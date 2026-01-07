#include "vm.hpp"
#include "reference.hpp"
#include "workspace.hpp"
#include <cstdlib>
#include <filesystem>
#include <map>

namespace {
    struct Outcome {
        long long value{0};
        int error{-1};
        std::string function;
        uint32_t pc{0};
        uint64_t instructions{0};
        mplx::SourceSpan span{};

        bool operator==(const Outcome &) const = default;
    };

    Outcome execute(const std::shared_ptr<const mplx::VerifiedModule> &module, mplx::VM::JitMode mode) {
        mplx::VM vm(mplx::CompiledProgram::create(module));
        vm.setJitMode(mode);
        vm.setLimits({1000, 16, 1024});
        Outcome result;
        try {
            result.value = vm.run();
        } catch (const mplx::RuntimeError &error) {
            result.error = static_cast<int>(error.code());
            result.function = error.function();
            result.pc = error.pc();
            result.span = error.span();
        }
        result.instructions = vm.stats().instructions;
        return result;
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > 8192) {
        return 0;
    }
    size_t split = 0;
    while (split < size && data[split] != 0) {
        ++split;
    }
    if (split == size) {
        return 0;
    }
    const std::map<std::string, std::string> sources{
        {"main.mplx", std::string(reinterpret_cast<const char *>(data), split)},
        {"lib.mplx", std::string(reinterpret_cast<const char *>(data + split + 1), size - split - 1)}};
    // Provider access is closed: fuzzing never reads arbitrary import targets from disk.
    mplx::WorkspaceCompiler compiler(
        std::filesystem::current_path(), [&](const std::string &path) -> std::optional<std::string> {
            const auto found = sources.find(path);
            return found == sources.end() ? std::nullopt : std::optional(found->second);
        });
    const auto first = compiler.build("main.mplx");
    const auto second = compiler.build("main.mplx");
    if (first.ok() != second.ok() ||
        mplx::workspace_diagnostics_json(first) != mplx::workspace_diagnostics_json(second)) {
        std::abort();
    }
    if (!first.ok()) {
        return 0;
    }
    if (!second.statistics.artifact_hit || second.artifact != first.artifact) {
        std::abort();
    }
    const auto reference = execute(first.artifact, mplx::VM::JitMode::Off);
    // Unreachable files may contain arbitrary malformed bytes. The workspace
    // validates only the entry's dependency closure, so the oracle receives that
    // same source set and independently resolves calls/imports within it.
    std::map<std::string, std::string> reachable_sources;
    for (const auto &file : first.files)
        reachable_sources.emplace(file.path, file.source);
    mplx::reference::Program oracle(std::move(reachable_sources), "main.mplx");
    bool completed = false;
    long long source_value = 0;
    int source_error = -1;
    std::string source_function;
    mplx::SourceSpan source_span{};
    try {
        source_value = oracle.invoke("main", {}, {10000, 16, 1024});
        completed = true;
    } catch (const mplx::reference::Error &error) {
        if (error.kind() == mplx::reference::ErrorKind::Arithmetic) {
            source_error = static_cast<int>(mplx::RuntimeErrorCode::Arithmetic);
            source_function = error.function();
            source_span = error.span();
            completed = true;
        } else if (error.kind() == mplx::reference::ErrorKind::InvalidEntry) {
            source_error = static_cast<int>(mplx::RuntimeErrorCode::InvalidEntry);
            completed = true;
        } else if (error.kind() == mplx::reference::ErrorKind::InvalidSource) {
            std::abort();
        }
    }
    const bool bytecode_completed = reference.error == -1 ||
                                    reference.error == static_cast<int>(mplx::RuntimeErrorCode::Arithmetic) ||
                                    reference.error == static_cast<int>(mplx::RuntimeErrorCode::InvalidEntry);
    if (completed && bytecode_completed &&
        (source_value != reference.value || source_error != reference.error))
        std::abort();
    if (completed && source_error == static_cast<int>(mplx::RuntimeErrorCode::Arithmetic) &&
        bytecode_completed && (source_function != reference.function || source_span != reference.span))
        std::abort();
#if defined(MPLX_WITH_JIT)
    if (!(reference == execute(second.artifact, mplx::VM::JitMode::On))) {
        std::abort();
    }
#else
    (void)reference;
#endif
    return 0;
}
