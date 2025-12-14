#include "capi.hpp"
#include "frontend.hpp"
#include "module_format.hpp"
#include "vm.hpp"
#include "workspace.hpp"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string_view>
static_assert(sizeof(long long) == sizeof(int64_t));
struct mplx_program {
    std::shared_ptr<const mplx::CompiledProgram> value;
};
struct mplx_workspace {
    mplx::WorkspaceCompiler compiler;
    std::map<std::string, std::weak_ptr<const mplx::CompiledProgram>> programs;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    explicit mplx_workspace(const std::filesystem::path &root) : compiler(root) {
    }
};
struct mplx_instance {
    const std::shared_ptr<const mplx::CompiledProgram> program;
    mplx::VM vm;
    const bool cancellable;
    std::atomic_bool cancellation{false};
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    mplx_instance(std::shared_ptr<const mplx::CompiledProgram> p, uint32_t jit, bool cancel)
        : program(std::move(p)), vm(program), cancellable(cancel) {
        vm.setJitMode(jit == MPLX_JIT_OFF  ? mplx::VM::JitMode::Off
                      : jit == MPLX_JIT_ON ? mplx::VM::JitMode::On
                                           : mplx::VM::JitMode::Auto);
        if (cancellable)
            vm.setCancellationFlag(&cancellation);
    }
};
namespace {
    char *duplicate(std::string_view value) noexcept {
        auto *data = static_cast<char *>(std::malloc(value.size() + 1));
        if (data) {
            std::memcpy(data, value.data(), value.size());
            data[value.size()] = 0;
        }
        return data;
    }
    int error_result(char **error, std::string_view text, int status) noexcept {
        if (error)
            *error = duplicate(text);
        return error && *error ? status : MPLX_ALLOCATION_FAILURE;
    }
    template <class Action> int boundary(char **error, Action action, bool typed = false) noexcept {
        if (error)
            *error = nullptr;
        if (!error)
            return MPLX_INVALID_ARGUMENT;
        try {
            return action();
        } catch (const std::bad_alloc &) {
            return error_result(error, "allocation failure", MPLX_ALLOCATION_FAILURE);
        } catch (const mplx::ModuleFormatError &e) {
            return error_result(error, e.what(), MPLX_INVALID_MODULE);
        } catch (const mplx::RuntimeError &e) {
            if (!typed)
                return error_result(error, e.what(), MPLX_RUNTIME_FAILURE);
            try {
                auto json =
                    "{\"code\":\"MPLX_RUNTIME\",\"kind\":" + std::to_string(static_cast<int>(e.code())) +
                    ",\"function\":" + mplx::json_quote(e.function()) + ",\"pc\":" + std::to_string(e.pc()) +
                    ",\"range\":" + mplx::span_json(e.span()) + ",\"message\":" + mplx::json_quote(e.what()) +
                    "}";
                int status = e.code() == mplx::RuntimeErrorCode::Cancelled      ? MPLX_CANCELLED
                             : e.code() == mplx::RuntimeErrorCode::InvalidEntry ? MPLX_INVALID_ARGUMENT
                                                                                : MPLX_RUNTIME_FAILURE;
                return error_result(error, json, status);
            } catch (...) {
                return error_result(error, "allocation failure", MPLX_ALLOCATION_FAILURE);
            }
        } catch (const std::invalid_argument &e) {
            return error_result(error, e.what(), MPLX_INVALID_ARGUMENT);
        } catch (const std::exception &e) {
            return error_result(error, e.what(), MPLX_RUNTIME_FAILURE);
        } catch (...) {
            return error_result(error, "unknown native failure", MPLX_RUNTIME_FAILURE);
        }
    }
    mplx::ExecutionLimits limits_from(const mplx_runtime_options *options) {
        if (!options)
            return {};
        if (options->struct_size != sizeof(*options) || options->abi_version != MPLX_ABI_VERSION ||
            !options->max_instructions || !options->max_frames || !options->max_values ||
            options->max_frames > std::numeric_limits<size_t>::max() ||
            options->max_values > std::numeric_limits<size_t>::max())
            throw std::invalid_argument("invalid runtime options or ABI version");
        return {options->max_instructions, static_cast<size_t>(options->max_frames),
                static_cast<size_t>(options->max_values)};
    }
    void stats_to(const mplx::ExecutionStats &value, mplx_execution_stats *out) {
        if (!out)
            return;
        out->instructions = value.instructions;
        out->peak_operands = value.peak_operands;
        out->peak_locals = value.peak_locals;
        out->peak_frames = value.peak_frames;
        out->jit_compilations = value.jit_compilations;
        out->jit_executions = value.jit_executions;
        out->jit_fallbacks = value.jit_fallbacks;
        out->native_calls = value.native_calls;
    }
    struct InvocationLease {
        mplx_instance *instance;
        explicit InvocationLease(mplx_instance *value) : instance(value) {
        }
        ~InvocationLease() {
            instance->busy.clear(std::memory_order_release);
        }
    };
} // namespace
extern "C" {
void mplx_free(char *data) {
    std::free(data);
}
void mplx_buffer_free(void *data) {
    std::free(data);
}
int mplx_check_source(const char *source, char **json, char **error) {
    if (json)
        *json = nullptr;
    return boundary(error, [&]() -> int {
        if (!source || !json)
            return MPLX_INVALID_ARGUMENT;
        auto result = mplx::analyze_source(source);
        *json = duplicate(mplx::diagnostics_json(result.diagnostics));
        return *json ? MPLX_OK : MPLX_ALLOCATION_FAILURE;
    });
}
int mplx_run_from_source_ex(const char *source, const char *entry, const mplx_runtime_options *options,
                            long long *value, char **error) {
    if (value)
        *value = 0;
    return boundary(error, [&]() -> int {
        if (!source || !entry || !value)
            return MPLX_INVALID_ARGUMENT;
        auto limits = limits_from(options);
        auto analyzed = mplx::analyze_source(source);
        if (!analyzed.ok())
            return error_result(error, mplx::diagnostics_json(analyzed.diagnostics), MPLX_SOURCE_DIAGNOSTICS);
        mplx::VM vm(std::move(analyzed.bytecode));
        vm.setJitMode(mplx::VM::JitMode::Off);
        vm.setLimits(limits);
        *value = vm.run(entry);
        return MPLX_OK;
    });
}
int mplx_run_from_source(const char *source, const char *entry, long long *value, char **error) {
    return mplx_run_from_source_ex(source, entry, nullptr, value, error);
}
int mplx_program_compile(const uint8_t *source, size_t size, uint32_t optimize, mplx_program **program,
                         char **error) {
    if (program)
        *program = nullptr;
    return boundary(error, [&]() -> int {
        if (!source || !program || size > 1024 * 1024 || optimize > 1)
            return MPLX_INVALID_ARGUMENT;
        if (std::memchr(source, 0, size))
            throw std::invalid_argument("source contains embedded NUL");
        auto analyzed =
            mplx::analyze_source(std::string(reinterpret_cast<const char *>(source), size), optimize != 0);
        if (!analyzed.ok())
            return error_result(error, mplx::diagnostics_json(analyzed.diagnostics), MPLX_SOURCE_DIAGNOSTICS);
        *program = new mplx_program{mplx::CompiledProgram::create(std::move(analyzed.bytecode))};
        return MPLX_OK;
    });
}
int mplx_program_compile_file(const char *root, const char *entry, uint32_t optimize, mplx_program **program,
                              char **error) {
    if (program) {
        *program = nullptr;
    }
    return boundary(error, [&]() -> int {
        if (!root || !entry || !program || optimize > 1) {
            return MPLX_INVALID_ARGUMENT;
        }
        mplx::WorkspaceCompiler compiler(mplx::utf8_path(root));
        auto result = compiler.build(entry, {}, optimize != 0);
        if (!result.ok()) {
            return error_result(error, mplx::workspace_diagnostics_json(result), MPLX_SOURCE_DIAGNOSTICS);
        }
        *program = new mplx_program{mplx::CompiledProgram::create(std::move(result.artifact))};
        return MPLX_OK;
    });
}
int mplx_program_load(const uint8_t *bytes, size_t size, mplx_program **program, char **error) {
    if (program)
        *program = nullptr;
    return boundary(error, [&]() -> int {
        if (!bytes || !program)
            return MPLX_INVALID_ARGUMENT;
        *program = new mplx_program{mplx::CompiledProgram::create(mplx::deserialize_module({bytes, size}))};
        return MPLX_OK;
    });
}
int mplx_program_save(const mplx_program *program, uint8_t **bytes, size_t *size, char **error) {
    if (bytes)
        *bytes = nullptr;
    if (size)
        *size = 0;
    return boundary(error, [&]() -> int {
        if (!program || !bytes || !size)
            return MPLX_INVALID_ARGUMENT;
        auto data = mplx::serialize_module(program->value->module());
        auto *result = static_cast<uint8_t *>(std::malloc(data.size()));
        if (!result)
            return MPLX_ALLOCATION_FAILURE;
        std::memcpy(result, data.data(), data.size());
        *bytes = result;
        *size = data.size();
        return MPLX_OK;
    });
}
int mplx_program_info(const mplx_program *program, char **json, char **error) {
    if (json)
        *json = nullptr;
    return boundary(error, [&]() -> int {
        if (!program || !json)
            return MPLX_INVALID_ARGUMENT;
        *json = duplicate(mplx::module_info_json(program->value->module()));
        return *json ? MPLX_OK : MPLX_ALLOCATION_FAILURE;
    });
}
void mplx_program_destroy(mplx_program *program) {
    delete program;
}
int mplx_workspace_create(const char *root, mplx_workspace **workspace, char **error) {
    if (workspace) {
        *workspace = nullptr;
    }
    return boundary(error, [&]() -> int {
        if (!root || !workspace) {
            return MPLX_INVALID_ARGUMENT;
        }
        *workspace = new mplx_workspace(mplx::utf8_path(root));
        return MPLX_OK;
    });
}
void mplx_workspace_destroy(mplx_workspace *workspace) {
    delete workspace;
}
int mplx_workspace_compile(mplx_workspace *workspace, const char *entry, const mplx_source_overlay *overlays,
                           size_t count, uint32_t optimize, mplx_program **program, char **statistics,
                           char **error) {
    if (program) {
        *program = nullptr;
    }
    if (statistics) {
        *statistics = nullptr;
    }
    return boundary(error, [&]() -> int {
        if (!workspace || !entry || !program || !statistics || optimize > 1 || count > 256 ||
            (!overlays && count)) {
            return MPLX_INVALID_ARGUMENT;
        }
        if (workspace->busy.test_and_set(std::memory_order_acquire)) {
            return error_result(error, "workspace already compiling", MPLX_BUSY);
        }
        struct Lease {
            std::atomic_flag &flag;
            ~Lease() {
                flag.clear(std::memory_order_release);
            }
        } lease{workspace->busy};
        std::vector<mplx::SourceOverlay> sources;
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            const auto &overlay = overlays[i];
            if (!overlay.path_utf8 || (!overlay.source_utf8 && overlay.source_size) ||
                overlay.source_size > 1024 * 1024 || overlay.source_size > 16 * 1024 * 1024 - total) {
                return MPLX_INVALID_ARGUMENT;
            }
            std::string source;
            if (overlay.source_size) {
                source.assign(reinterpret_cast<const char *>(overlay.source_utf8), overlay.source_size);
            }
            if (source.find('\0') != std::string::npos) {
                throw std::invalid_argument("overlay contains embedded NUL");
            }
            sources.push_back({overlay.path_utf8, std::move(source)});
            total += overlay.source_size;
        }
        auto result = workspace->compiler.build(entry, sources, optimize != 0);
        auto summary = duplicate(mplx::workspace_statistics_json(result.statistics));
        if (!summary) {
            return MPLX_ALLOCATION_FAILURE;
        }
        *statistics = summary;
        if (!result.ok()) {
            return error_result(error, mplx::workspace_diagnostics_json(result), MPLX_SOURCE_DIAGNOSTICS);
        }
        auto compiled = workspace->programs[result.entry].lock();
        if (!compiled || &compiled->module() != result.artifact.get()) {
            compiled = mplx::CompiledProgram::create(std::move(result.artifact));
            workspace->programs[result.entry] = compiled;
        }
        while (workspace->programs.size() > 8) {
            workspace->programs.erase(workspace->programs.begin());
        }
        *program = new mplx_program{std::move(compiled)};
        return MPLX_OK;
    });
}
int mplx_instance_create(const mplx_program *program, const mplx_instance_options *options,
                         mplx_instance **instance, char **error) {
    if (instance)
        *instance = nullptr;
    return boundary(error, [&]() -> int {
        if (!program || !instance)
            return MPLX_INVALID_ARGUMENT;
        uint32_t jit = MPLX_JIT_OFF, flags = MPLX_INSTANCE_CANCELLABLE;
        if (options) {
            if (options->struct_size != sizeof(*options) || options->abi_version != MPLX_ABI_VERSION ||
                options->jit_mode > MPLX_JIT_AUTO || (options->flags & ~uint32_t(MPLX_INSTANCE_CANCELLABLE)))
                throw std::invalid_argument("invalid instance options or ABI version");
            jit = options->jit_mode;
            flags = options->flags;
        }
        *instance = new mplx_instance(program->value, jit, (flags & MPLX_INSTANCE_CANCELLABLE) != 0);
        return MPLX_OK;
    });
}
void mplx_instance_destroy(mplx_instance *instance) {
    delete instance;
}
int mplx_instance_cancel(mplx_instance *instance) {
    if (!instance || !instance->cancellable)
        return MPLX_INVALID_ARGUMENT;
    instance->cancellation.store(true, std::memory_order_relaxed);
    return MPLX_OK;
}
uint32_t mplx_instance_is_running(const mplx_instance *instance) {
    return instance && instance->busy.test(std::memory_order_acquire) ? 1u : 0u;
}
int mplx_instance_reset_cancel(mplx_instance *instance) {
    if (!instance || !instance->cancellable)
        return MPLX_INVALID_ARGUMENT;
    // Reset must not revoke a cancellation already observed by a running invocation.
    if (instance->busy.test_and_set(std::memory_order_acquire))
        return MPLX_BUSY;
    InvocationLease lease(instance);
    instance->cancellation.store(false, std::memory_order_relaxed);
    return MPLX_OK;
}
int mplx_instance_invoke(mplx_instance *instance, const char *entry, const int64_t *arguments, size_t count,
                         const mplx_runtime_options *options, int64_t *value, mplx_execution_stats *stats,
                         char **error) {
    if (value)
        *value = 0;
    const bool valid_stats =
        !stats || (stats->struct_size == sizeof(*stats) && stats->abi_version == MPLX_ABI_VERSION);
    if (valid_stats && stats)
        stats_to({}, stats);
    return boundary(
        error,
        [&]() -> int {
            if (!instance || !entry || !value || !valid_stats || count > 255 || (!arguments && count))
                return MPLX_INVALID_ARGUMENT;
            auto limits = limits_from(options);
            if (instance->busy.test_and_set(std::memory_order_acquire))
                return error_result(error, "instance already executing", MPLX_BUSY);
            InvocationLease lease(instance);
            instance->vm.setLimits(limits);
            std::array<long long, 255> values{};
            for (size_t p = 0; p < count; ++p)
                values[p] = arguments[p];
            try {
                *value = instance->vm.run(entry, std::span(values).first(count));
                stats_to(instance->vm.stats(), stats);
            } catch (...) {
                stats_to(instance->vm.stats(), stats);
                throw;
            }
            return MPLX_OK;
        },
        true);
}
}
