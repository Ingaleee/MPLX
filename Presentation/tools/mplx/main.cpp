#include "frontend.hpp"
#include "module_format.hpp"
#include "verifier.hpp"
#include "vm.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace {
    struct Options {
        std::string mode, file, entry{"main"}, jit{"auto"}, bench_mode{"compile-run"};
        fs::path output;
        bool no_runfile{false}, optimize{true}, trace{false}, stats{false}, jit_verify{false},
            jit_dump{false};
        uint64_t trace_limit{0};
        uint32_t hot{1}, runs{20}, warmup{3};
        mplx::ExecutionLimits limits;
        std::vector<long long> arguments;
        std::string root;
        std::string pass_name;
    };
    void usage() {
        std::cout << "MPLX " MPLX_VERSION "\n"
                     "Usage: mplx <command> [options] <file>\n"
                     "Commands: --run --check --symbols --analyze --bench --dump-ir --dump-passes "
                     "--dump-bytecode --dump-cfg --dump-machine --dump-analyses\n"
                     "          --dump-pass-trace --snapshot-ir --replay-pass\n"
                     "          --build --run-module --inspect-module --workspace\n"
                     "Options: --entry NAME --jit off|on|auto --hot N --jit-verify --jit-dump\n"
                     "         --max-steps N --max-frames N --max-values N --trace --trace-limit N\n"
                     "         --stats --no-opt --no-runfile --out PATH\n"
                     "         --arg I64 (repeat for exported function arguments) --root PATH\n"
                     "Replay:  --replay-pass --pass NAME [--out AFTER.mplxir] BEFORE.mplxir\n"
                     "Bench:   --mode compile-run|run-only --runs N --warmup N --json\n";
    }
    uint64_t number(const std::string &text, bool zero = false) {
        uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || (!zero && value == 0))
            throw std::runtime_error("invalid numeric option: " + text);
        return value;
    }
    Options parse(int argc, char **argv) {
        Options o;
        o.mode = argv[1];
        if (o.mode != "--run" && o.mode != "--check" && o.mode != "--symbols" && o.mode != "--analyze" &&
            o.mode != "--bench" && o.mode != "--dump-ir" && o.mode != "--dump-bytecode" &&
            o.mode != "--dump-cfg" && o.mode != "--build" && o.mode != "--run-module" &&
            o.mode != "--inspect-module" && o.mode != "--workspace" && o.mode != "--dump-passes" &&
            o.mode != "--dump-machine" && o.mode != "--dump-analyses" && o.mode != "--dump-pass-trace" &&
            o.mode != "--snapshot-ir" && o.mode != "--replay-pass")
            throw std::runtime_error("unknown command: " + o.mode);
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 == argc)
                    throw std::runtime_error("missing value for " + a);
                return argv[++i];
            };
            if (a == "--root")
                o.root = next();
            else if (a == "--entry")
                o.entry = next();
            else if (a == "--pass")
                o.pass_name = next();
            else if (a == "--arg") {
                auto value = next();
                long long argument = 0;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), argument);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    o.arguments.size() >= 255)
                    throw std::runtime_error("invalid function argument");
                o.arguments.push_back(argument);
            } else if (a == "--jit")
                o.jit = next();
            else if (a == "--mode")
                o.bench_mode = next();
            else if (a == "--out")
                o.output = next();
            else if (a.rfind("--out=", 0) == 0)
                o.output = a.substr(6);
            else if (a == "--hot") {
                auto v = number(next());
                if (v > UINT32_MAX)
                    throw std::runtime_error("--hot too large");
                o.hot = static_cast<uint32_t>(v);
            } else if (a == "--runs") {
                auto v = number(next());
                if (v > 100000)
                    throw std::runtime_error("--runs exceeds 100000");
                o.runs = static_cast<uint32_t>(v);
            } else if (a == "--warmup") {
                auto v = number(next(), true);
                if (v > 100000)
                    throw std::runtime_error("--warmup exceeds 100000");
                o.warmup = static_cast<uint32_t>(v);
            } else if (a == "--max-steps")
                o.limits.instructions = number(next());
            else if (a == "--max-frames") {
                auto v = number(next());
                if (v > SIZE_MAX)
                    throw std::runtime_error("--max-frames too large");
                o.limits.frames = static_cast<size_t>(v);
            } else if (a == "--max-values") {
                auto v = number(next());
                if (v > SIZE_MAX)
                    throw std::runtime_error("--max-values too large");
                o.limits.values = static_cast<size_t>(v);
            } else if (a == "--trace-limit")
                o.trace_limit = number(next(), true);
            else if (a == "--trace")
                o.trace = true;
            else if (a == "--stats")
                o.stats = true;
            else if (a == "--no-opt")
                o.optimize = false;
            else if (a == "--no-runfile")
                o.no_runfile = true;
            else if (a == "--jit-verify")
                o.jit_verify = true;
            else if (a == "--jit-dump")
                o.jit_dump = true;
            else if (a == "--json") {
            } // benchmark output is always machine readable
            else if (!a.empty() && a[0] == '-')
                throw std::runtime_error("unknown option: " + a);
            else if (o.file.empty())
                o.file = a;
            else
                throw std::runtime_error("multiple input files");
        }
        if (o.file.empty())
            throw std::runtime_error("missing input file");
        if (o.jit != "off" && o.jit != "on" && o.jit != "auto")
            throw std::runtime_error("invalid JIT mode");
        if (o.bench_mode != "compile-run" && o.bench_mode != "run-only")
            throw std::runtime_error("invalid benchmark mode");
#if !defined(MPLX_WITH_JIT)
        if (o.jit == "on" || o.jit_verify || o.jit_dump)
            throw std::runtime_error("this build has no JIT; configure MPLX_WITH_JIT=ON");
#endif
        return o;
    }
    std::string read_source(const std::string &path, size_t maximum = 1024 * 1024) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot open file: " + path);
        input.seekg(0, std::ios::end);
        auto size = input.tellg();
        if (size < 0 || static_cast<uint64_t>(size) > maximum)
            throw std::runtime_error("input exceeds size limit or cannot be read");
        input.seekg(0);
        std::string source(static_cast<size_t>(size), '\0');
        if (!source.empty() && !input.read(source.data(), static_cast<std::streamsize>(source.size())))
            throw std::runtime_error("cannot read file: " + path);
        return source;
    }
    void write_atomic(const fs::path &target, std::string_view text) {
        if (!target.parent_path().empty())
            fs::create_directories(target.parent_path());
        auto tmp = target;
        tmp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
        try {
            {
                std::ofstream out(tmp, std::ios::binary);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.flush();
                if (!out)
                    throw std::runtime_error("cannot write result file");
            }
#if defined(_WIN32)
            if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("cannot replace result file");
#else
            fs::rename(tmp, target);
#endif
        } catch (...) {
            std::error_code error;
            fs::remove(tmp, error);
            throw;
        }
    }
    void write_result(const Options &o, const std::string &text) {
        if (o.no_runfile)
            return;
        write_atomic(o.output.empty() ? fs::path(o.file).parent_path() / "run.txt" : o.output, text + '\n');
    }
    void configure(mplx::VM &vm, const Options &o) {
        vm.setJitMode(o.jit == "off"  ? mplx::VM::JitMode::Off
                      : o.jit == "on" ? mplx::VM::JitMode::On
                                      : mplx::VM::JitMode::Auto);
        vm.setHotThreshold(o.hot);
        vm.setLimits(o.limits);
        vm.setTrace(o.trace);
        vm.setTraceLimit(o.trace_limit);
    }
    std::string stats_json(const mplx::ExecutionStats &s) {
        return "{\"instructions\":" + std::to_string(s.instructions) +
               ",\"peak_operands\":" + std::to_string(s.peak_operands) +
               ",\"peak_locals\":" + std::to_string(s.peak_locals) +
               ",\"peak_frames\":" + std::to_string(s.peak_frames) +
               ",\"jit_compilations\":" + std::to_string(s.jit_compilations) +
               ",\"jit_executions\":" + std::to_string(s.jit_executions) +
               ",\"jit_fallbacks\":" + std::to_string(s.jit_fallbacks) +
               ",\"native_calls\":" + std::to_string(s.native_calls) + "}";
    }
    mplx::AnalysisResult analyze_file(const Options &options, const std::string &source,
                                      mplx::WorkspaceResult *workspace = nullptr) {
        auto result = mplx::analyze_source(source, options.optimize);
        if (result.module.imports.empty() && options.mode != "--workspace" && options.root.empty()) {
            return result;
        }
        const auto root =
            options.root.empty() ? fs::current_path() : fs::absolute(mplx::utf8_path(options.root));
        const auto path = fs::absolute(mplx::utf8_path(options.file)).generic_u8string();
        const std::string entry(reinterpret_cast<const char *>(path.data()), path.size());
        mplx::WorkspaceCompiler compiler(root);
        const std::array overlays{mplx::SourceOverlay{entry, source}};
        auto project = compiler.build(entry, overlays, options.optimize,
                                      options.mode != "--workspace" && options.mode != "--check" &&
                                          options.mode != "--analyze");
        result.diagnostics.clear();
        for (const auto &diagnostic : project.diagnostics) {
            auto value = diagnostic.diagnostic;
            value.message = diagnostic.path + ": " + value.message;
            result.diagnostics.push_back(std::move(value));
        }
        for (const auto &file : project.files) {
            if (file.path == project.entry) {
                result.semantic = file.semantic;
            }
        }
        result.ir = project.ir;
        result.optimization = project.optimization;
        if (project.artifact) {
            result.bytecode = project.artifact->bytecode();
        }
        if (workspace) {
            *workspace = std::move(project);
        }
        return result;
    }
    mplx::AnalysisResult analyzed(const Options &options, const std::string &source) {
        auto result = analyze_file(options, source);
        if (!result.ok())
            throw std::runtime_error(mplx::diagnostics_json(result.diagnostics));
        return result;
    }
    void write_compiler_artifact(const Options &options, std::string_view bytes,
                                 const mplx::WorkspaceResult &workspace = {}) {
        if (options.output.empty())
            throw std::runtime_error("compiler artifact requires --out PATH");
        if (fs::exists(options.output)) {
            if (fs::equivalent(options.output, mplx::utf8_path(options.file)))
                throw std::runtime_error("compiler output must differ from source");
            const auto root =
                options.root.empty() ? fs::current_path() : fs::absolute(mplx::utf8_path(options.root));
            for (const auto &file : workspace.files)
                if (fs::equivalent(options.output, root / mplx::utf8_path(file.path)))
                    throw std::runtime_error("compiler output must differ from every dependency source");
        }
        write_atomic(options.output, bytes);
    }
    double percentile(const std::vector<double> &sorted, double p) {
        auto position = (sorted.size() - 1) * p;
        auto lo = static_cast<size_t>(position), hi = std::min(lo + 1, sorted.size() - 1);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * (position - lo);
    }
    int benchmark(const Options &o, const std::string &source) {
        auto a = analyzed(o, source);
        mplx::VM prepared(std::move(a.bytecode));
        configure(prepared, o);
        std::vector<double> samples;
        uint64_t native_runs = 0, compilations = 0, native_calls = 0;
        long long result = 0;
        auto iteration = [&]() {
            if (o.bench_mode == "run-only") {
                result = prepared.run(o.entry, o.arguments);
                return prepared.stats();
            }
            auto current = analyzed(o, source);
            mplx::VM vm(std::move(current.bytecode));
            configure(vm, o);
            result = vm.run(o.entry, o.arguments);
            return vm.stats();
        };
        for (uint32_t i = 0; i < o.warmup; ++i)
            (void)iteration();
        for (uint32_t i = 0; i < o.runs; ++i) {
            auto begin = std::chrono::steady_clock::now();
            auto stats = iteration();
            auto end = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
            native_runs += stats.jit_executions;
            compilations += stats.jit_compilations;
            native_calls += stats.native_calls;
        }
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        std::cout << std::setprecision(10) << "{\"schema_version\":1,\"file\":" << mplx::json_quote(o.file)
                  << ",\"mode\":" << mplx::json_quote(o.bench_mode) << ",\"runs\":" << o.runs
                  << ",\"warmup\":" << o.warmup << ",\"result\":" << result
                  << ",\"optimized\":" << (o.optimize ? "true" : "false")
                  << ",\"requested_jit\":" << mplx::json_quote(o.jit) << ",\"native_runs\":" << native_runs
                  << ",\"jit_compilations\":" << compilations << ",\"native_calls\":" << native_calls
                  << ",\"version\":" << mplx::json_quote(MPLX_VERSION)
                  << ",\"avg_ms\":" << std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size()
                  << ",\"best_ms\":" << sorted.front() << ",\"worst_ms\":" << sorted.back()
                  << ",\"p50_ms\":" << percentile(sorted, 0.50) << ",\"p95_ms\":" << percentile(sorted, 0.95)
#if defined(_MSC_VER)
                  << ",\"compiler\":\"MSVC " << _MSC_VER << "\""
#else
                  << ",\"compiler\":" << mplx::json_quote(__VERSION__)
#endif
                  << ",\"build_type\":" << mplx::json_quote(MPLX_BUILD_CONFIGURATION) << ",\"samples_ms\":[";
        for (size_t i = 0; i < samples.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << samples[i];
        }
        std::cout << "]}\n";
        return 0;
    }
    int analysis_server() {
#if defined(_WIN32)
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::unique_ptr<mplx::WorkspaceCompiler> workspace;
        std::string workspace_root;
        auto payload = [](size_t size) {
            std::string value(size, '\0');
            if (size && !std::cin.read(value.data(), static_cast<std::streamsize>(size))) {
                throw std::runtime_error("truncated compiler service frame");
            }
            return value;
        };
        std::string header;
        auto read_header = [](std::string &line) {
            line.clear();
            char value = 0;
            while (std::cin.get(value)) {
                if (value == '\n') {
                    return true;
                }
                if (line.size() >= 256) {
                    throw std::runtime_error("compiler frame header exceeds 256 bytes");
                }
                line.push_back(value);
            }
            if (!line.empty()) {
                throw std::runtime_error("truncated compiler frame header");
            }
            return false;
        };
        for (;;) {
            try {
                if (!read_header(header)) {
                    break;
                }
            } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                return 2;
            }
            std::istringstream input(header);
            std::string command, extra;
            uint64_t id = 0;
            if (!(input >> command >> id)) {
                return 2;
            }
            std::string result;
            try {
                if (command == "ANALYZE") {
                    uint64_t size = 0;
                    if (!(input >> size) || size > 1024 * 1024 || (input >> extra)) {
                        return 2;
                    }
                    result =
                        mplx::analysis_json(mplx::analyze_source(payload(static_cast<size_t>(size)), false));
                } else if (command == "WORKSPACE") {
                    uint64_t root_size = 0, entry_size = 0, count = 0;
                    if (!(input >> root_size >> entry_size >> count) || !root_size || !entry_size ||
                        root_size > 4096 || entry_size > 4096 || count > 256 || (input >> extra)) {
                        return 2;
                    }
                    const auto root = payload(static_cast<size_t>(root_size));
                    const auto entry = payload(static_cast<size_t>(entry_size));
                    std::vector<mplx::SourceOverlay> overlays;
                    size_t total = 0;
                    for (uint64_t i = 0; i < count; ++i) {
                        if (!read_header(header)) {
                            return 2;
                        }
                        std::istringstream record(header);
                        uint64_t path_size = 0, source_size = 0;
                        if (!(record >> command >> path_size >> source_size) || command != "FILE" ||
                            !path_size || path_size > 4096 || source_size > 1024 * 1024 ||
                            source_size > 16 * 1024 * 1024 - total || (record >> extra)) {
                            return 2;
                        }
                        auto path = payload(static_cast<size_t>(path_size));
                        auto source = payload(static_cast<size_t>(source_size));
                        total += static_cast<size_t>(source_size);
                        overlays.push_back({std::move(path), std::move(source)});
                    }
                    if (!workspace || workspace_root != root) {
                        workspace = std::make_unique<mplx::WorkspaceCompiler>(mplx::utf8_path(root));
                        workspace_root = root;
                    }
                    result = mplx::workspace_json(workspace->build(entry, overlays, false, false));
                } else {
                    return 2;
                }
            } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                return 2;
            }
            if (result.size() > 32 * 1024 * 1024) {
                std::cerr << "compiler response exceeds 32 MiB\n";
                return 2;
            }
            std::cout << "{\"id\":" << id << ",\"result\":" << result << "}\n" << std::flush;
        }
        return 0;
    }
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--analysis-server")
        return analysis_server();
    if (argc < 2 || std::string(argv[1]) == "--help") {
        usage();
        return 0;
    }
    if (std::string(argv[1]) == "--version") {
        std::cout << "mplx " MPLX_VERSION "\n";
        return 0;
    }
    Options o;
    try {
        o = parse(argc, argv);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 2;
    }
    try {
        const bool from_module = o.mode == "--run-module" || o.mode == "--inspect-module";
        const auto source = read_source(o.file, o.mode == "--replay-pass" ? mplx::ir_snapshot_limit
                                                : from_module             ? mplx::max_module_bytes
                                                                          : 1024 * 1024);
        if (o.mode == "--replay-pass") {
            if (o.pass_name.empty())
                throw std::runtime_error("--replay-pass requires --pass NAME");
            auto ir = mplx::deserialize_ir_snapshot(source);
            const auto report = mplx::run_optimization_pipeline(ir, {true}, o.pass_name);
            if (!o.output.empty())
                write_compiler_artifact(o, mplx::serialize_ir_snapshot(ir));
            std::cout << mplx::optimization_trace_json(report) << '\n';
            return 0;
        }
        if (!o.pass_name.empty())
            throw std::runtime_error("--pass is only valid with --replay-pass");
        if (o.mode == "--dump-pass-trace")
            o.optimize = false;
        std::shared_ptr<const mplx::CompiledProgram> program;
        if (from_module) {
            auto module =
                mplx::deserialize_module({reinterpret_cast<const uint8_t *>(source.data()), source.size()});
            if (o.mode == "--inspect-module") {
                std::cout << mplx::module_info_json(*module) << '\n';
                return 0;
            }
            program = mplx::CompiledProgram::create(std::move(module));
        }
#if defined(MPLX_WITH_JIT)
        if (o.jit_dump) {
#if defined(_WIN32)
            _putenv_s("MPLX_JIT_DUMP", "1");
#else
            setenv("MPLX_JIT_DUMP", "1", 1);
#endif
        }
#endif
        if (o.mode == "--bench")
            return benchmark(o, source);
        mplx::WorkspaceResult workspace;
        auto a = from_module ? mplx::AnalysisResult{} : analyze_file(o, source, &workspace);
        if (o.mode == "--workspace") {
            std::cout << mplx::workspace_json(workspace) << '\n';
            return workspace.ok() ? 0 : 1;
        }
        if (o.mode == "--analyze") {
            std::cout << mplx::analysis_json(a) << '\n';
            return a.ok() ? 0 : 1;
        }
        if (o.mode == "--check") {
            std::cout << (workspace.entry.empty() ? mplx::diagnostics_json(a.diagnostics)
                                                  : mplx::workspace_diagnostics_json(workspace))
                      << '\n';
            return a.ok() ? 0 : 1;
        }
        if (!from_module && !a.ok())
            throw std::runtime_error(mplx::diagnostics_json(a.diagnostics));
        if (o.mode == "--build") {
            auto module = mplx::VerifiedModule::create(std::move(a.bytecode));
            auto bytes = mplx::serialize_module(*module);
            auto target = o.output.empty() ? fs::path(o.file).replace_extension(".mplxb") : o.output;
            if (fs::exists(target) && fs::equivalent(target, o.file))
                throw std::runtime_error("module output must differ from source");
            if (fs::exists(target) && !workspace.entry.empty()) {
                const auto root = o.root.empty() ? fs::current_path() : fs::absolute(mplx::utf8_path(o.root));
                for (const auto &file : workspace.files) {
                    if (fs::equivalent(target, root / mplx::utf8_path(file.path))) {
                        throw std::runtime_error("module output must differ from every dependency source");
                    }
                }
            }
            write_atomic(target, {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
            std::cout << "{\"format_version\":\"1.0\",\"file\":" << mplx::json_quote(target.string())
                      << ",\"bytes\":" << bytes.size() << "}\n";
            return 0;
        }
        if (o.mode == "--dump-ir") {
            std::cout << mplx::dump_ir_json(a.ir) << '\n';
            return 0;
        }
        if (o.mode == "--dump-analyses") {
            std::cout << mplx::dump_analyses_json(a.ir) << '\n';
            return 0;
        }
        if (o.mode == "--dump-pass-trace") {
            const auto report = mplx::run_optimization_pipeline(a.ir, {true});
            const auto trace = mplx::optimization_trace_json(report);
            if (!o.output.empty())
                write_compiler_artifact(o, trace, workspace);
            std::cout << trace << '\n';
            return 0;
        }
        if (o.mode == "--snapshot-ir") {
            const auto snapshot = mplx::serialize_ir_snapshot(a.ir);
            write_compiler_artifact(o, snapshot, workspace);
            std::cout << "{\"snapshot_version\":1,\"bytes\":" << snapshot.size()
                      << ",\"fingerprint\":" << mplx::json_quote(mplx::ir_fingerprint(a.ir)) << "}\n";
            return 0;
        }
        if (o.mode == "--dump-passes") {
            std::cout << mplx::optimization_report_json(a.optimization) << '\n';
            return 0;
        }
        if (o.mode == "--dump-machine") {
#if defined(MPLX_WITH_JIT)
            auto module = mplx::VerifiedModule::create(a.bytecode);
            auto machine = mplx::jit::lower_machine_ir(*module);
            mplx::jit::JitCompiler compiler;
            auto compiled = compiler.compileModule(*module);
            if (!compiled)
                throw std::runtime_error("native backend cannot compile this module");
            const auto &allocation = compiled->allocation;
            std::cout << "{\"machine_ir\":" << mplx::jit::dump_machine_ir_json(machine)
                      << ",\"allocation\":{\"registers\":4,\"memory_reads\":" << allocation.memory_reads
                      << ",\"memory_writes\":" << allocation.memory_writes
                      << ",\"register_reads\":" << allocation.register_reads
                      << ",\"register_writes\":" << allocation.register_writes
                      << ",\"spills\":" << allocation.spills
                      << ",\"coalesced_copies\":" << allocation.coalesced_copies
                      << "},\"native_bytes\":" << compiled->size << "}\n";
            return 0;
#else
            throw std::runtime_error("--dump-machine requires a JIT-enabled build");
#endif
        }
        if (o.mode == "--symbols") {
            std::cout << "{\"functions\":[";
            for (size_t i = 0; i < a.module.functions.size(); ++i) {
                if (i)
                    std::cout << ',';
                const auto &f = a.module.functions[i];
                std::cout << "{\"name\":" << mplx::json_quote(f.name) << ",\"arity\":" << f.params.size()
                          << '}';
            }
            std::cout << "]}\n";
            return 0;
        }
        if (o.mode == "--dump-bytecode") {
            std::cout << mplx::dump_bytecode_json(a.bytecode) << '\n';
            return 0;
        }
        if (o.mode == "--dump-cfg") {
            std::cout << mplx::dump_cfg_json(a.bytecode) << '\n';
            return 0;
        }
        if (!program)
            program = mplx::CompiledProgram::create(std::move(a.bytecode));
        mplx::VM vm(program);
        configure(vm, o);
        const auto value = vm.run(o.entry, o.arguments);
        if (o.jit_verify) {
            mplx::VM reference(program);
            configure(reference, o);
            reference.setJitMode(mplx::VM::JitMode::Off);
            if (reference.run(o.entry, o.arguments) != value) {
                std::cerr << "JIT verification mismatch\n";
                return 3;
            }
            std::cerr << "{\"jit_verify\":\"passed\",\"native_executions\":" << vm.stats().jit_executions
                      << "}\n";
        }
        write_result(o, std::to_string(value));
        std::cout << "Result: " << value << '\n';
        if (o.stats)
            std::cerr << stats_json(vm.stats()) << '\n';
        return 0;
    } catch (const std::exception &ex) {
        if (o.mode == "--check")
            std::cout << mplx::diagnostics_json({{"MPLX0001", ex.what(), 0, 0, {}}}) << '\n';
        else {
            std::cerr << ex.what() << '\n';
            if (o.mode == "--run" || o.mode == "--run-module") {
                try {
                    write_result(o, ex.what());
                } catch (const std::exception &write) {
                    std::cerr << write.what() << '\n';
                }
            }
        }
        return 1;
    }
}
