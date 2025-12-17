#include "module_format.hpp"
#include "vm.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>

namespace {
    size_t checks = 0;

    void expect(bool condition, const std::string &message) {
        ++checks;
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    using Sources = std::map<std::string, std::string>;

    mplx::SourceProvider provider(Sources &sources) {
        return [&](const std::string &path) -> std::optional<std::string> {
            const auto it = sources.find(path);
            return it == sources.end() ? std::nullopt : std::optional(it->second);
        };
    }

    long long execute(const mplx::WorkspaceResult &project, mplx::VM::JitMode mode) {
        expect(project.ok(), mplx::workspace_diagnostics_json(project));
        expect(bool(project.artifact), "missing linked artifact");
        auto program = mplx::CompiledProgram::create(project.artifact);
        mplx::VM vm(program);
        vm.setJitMode(mode);
        return vm.run();
    }

    void modules() {
        Sources sources{{"main.mplx", "import \"lib/math.mplx\" as math;"
                                      "fn main()->i64{return math::add(20,22);}"},
                        {"lib/math.mplx", "fn internal(a:i64,b:i64)->i64{return a+b;}"
                                          "export fn add(a:i64,b:i64)->i64{return internal(a,b);}"}};
        mplx::WorkspaceCompiler compiler(std::filesystem::current_path(), provider(sources));
        auto project = compiler.build("main.mplx");
        expect(project.files.size() == 2 && project.dependencies.size() == 1, "two-file graph");
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
            expect(execute(project, mode) == 42, "linked private implementation/native call");
        }
        const auto exported = std::find_if(project.files.begin(), project.files.end(),
                                           [](const auto &file) { return file.path == "lib/math.mplx"; });
        expect(exported != project.files.end(), "exporter snapshot");
        const auto caller = std::find_if(project.files.begin(), project.files.end(),
                                         [](const auto &file) { return file.path == "main.mplx"; });
        const auto imported = std::find_if(caller->semantic.symbols.begin(), caller->semantic.symbols.end(),
                                           [](const auto &symbol) { return symbol.imported; });
        expect(imported != caller->semantic.symbols.end() && imported->definition_path == "lib/math.mplx" &&
                   imported->original_name == "add",
               "cross-file symbol identity");
        const auto occurrences =
            std::count_if(caller->semantic.occurrences.begin(), caller->semantic.occurrences.end(),
                          [&](const auto &o) { return o.symbol == imported->id; });
        expect(occurrences == 1, "foreign declaration must not create a caller token");
        const auto call = caller->semantic.occurrences.back();
        expect(call.span.end.offset > call.span.start.offset, "member reference has source span");
        auto serialized = mplx::serialize_module(*project.artifact);
        mplx::VM loaded(mplx::CompiledProgram::create(mplx::deserialize_module(serialized)));
        expect(loaded.run() == 42, "linked artifact portable round trip");

        sources["main.mplx"] = "import \"lib/math.mplx\" as math;fn main()->i64{return math::internal(1,2);}";
        const auto private_call = compiler.build("main.mplx");
        expect(!private_call.ok(), "private function leaked across namespace boundary");
        expect(std::any_of(
                   private_call.diagnostics.begin(), private_call.diagnostics.end(),
                   [](const auto &d) { return d.path == "main.mplx" && d.diagnostic.code == "MPLX2016"; }),
               "private call diagnostic");
        sources["main.mplx"] = "import \"lib/math.mplx\" as m;import \"lib/math.mplx\" as m;"
                               "fn main()->i64{return 0;}";
        const auto duplicate = compiler.build("main.mplx");
        expect(std::any_of(duplicate.diagnostics.begin(), duplicate.diagnostics.end(),
                           [](const auto &d) { return d.diagnostic.code == "MPLX4005"; }),
               "duplicate namespace diagnostic");
    }

    void incremental() {
        Sources sources{
            {"main.mplx", "import \"bridge.mplx\" as b;fn main()->i64{return b::value();}"},
            {"bridge.mplx", "import \"leaf.mplx\" as l;export fn value()->i64{return l::value();}"},
            {"leaf.mplx", "export fn value()->i64{return 42;}"}};
        mplx::WorkspaceCompiler compiler(std::filesystem::current_path(), provider(sources));
        auto first = compiler.build("main.mplx");
        expect(first.statistics.parsed == 3 && first.statistics.checked == 3, "cold compilation");
        expect(!first.optimization.reused && first.optimization.passes.size() == 7, "cold linked SSA report");
        auto repeated = compiler.build("main.mplx");
        expect(repeated.statistics.parsed == 0 && repeated.statistics.checked == 0 &&
                   repeated.statistics.syntax_hits == 3 && repeated.statistics.semantic_hits == 3 &&
                   repeated.statistics.artifact_hit && repeated.artifact == first.artifact,
               "unchanged graph must reuse verified artifact");
        expect(repeated.optimization.reused && repeated.optimization.passes.size() == 7,
               "artifact hit must identify reused pass timings");

        sources["leaf.mplx"] = "export fn value()->i64{return 43;}";
        auto body = compiler.build("main.mplx");
        expect(body.statistics.parsed == 1 && body.statistics.checked == 1 &&
                   body.statistics.semantic_hits == 2 && !body.statistics.artifact_hit,
               "private implementation edit must not recheck callers");
        expect(execute(body, mplx::VM::JitMode::On) == 43, "relinked body update");
        const auto &caller = *std::find_if(body.files.begin(), body.files.end(),
                                           [](const auto &file) { return file.path == "bridge.mplx"; });
        const auto symbol = std::find_if(caller.semantic.symbols.begin(), caller.semantic.symbols.end(),
                                         [](const auto &s) { return s.imported; });
        sources["leaf.mplx"] =
            "// positions change independently of interface\nexport fn value()->i64{return 44;}";
        auto moved = compiler.build("main.mplx");
        const auto &moved_caller = *std::find_if(moved.files.begin(), moved.files.end(),
                                                 [](const auto &file) { return file.path == "bridge.mplx"; });
        const auto moved_symbol =
            std::find_if(moved_caller.semantic.symbols.begin(), moved_caller.semantic.symbols.end(),
                         [](const auto &s) { return s.imported; });
        expect(moved.statistics.checked == 1 &&
                   moved_symbol->declaration.start.offset > symbol->declaration.start.offset,
               "cached semantic facts must refresh foreign source locations");

        sources["leaf.mplx"] = "export fn value(n:i64)->i64{return n;}";
        const auto signature = compiler.build("main.mplx");
        expect(!signature.ok() && signature.statistics.checked == 2 &&
                   signature.statistics.semantic_hits == 1,
               "interface change rechecks direct dependents");
        expect(std::any_of(
                   signature.diagnostics.begin(), signature.diagnostics.end(),
                   [](const auto &d) { return d.path == "bridge.mplx" && d.diagnostic.code == "MPLX2018"; }),
               "dependent arity diagnostic location");

        const std::array overlay{mplx::SourceOverlay{"leaf.mplx", "export fn value()->i64{return 99;}"}};
        auto unsaved = compiler.build("main.mplx", overlay);
        expect(execute(unsaved, mplx::VM::JitMode::Off) == 99, "overlay overrides provider source");
        sources["leaf.mplx"] = "export fn value()->i64{return 42;}";
        expect(execute(compiler.build("main.mplx"), mplx::VM::JitMode::Off) == 42,
               "closed overlay returns to source provider");
    }

    void graph_errors() {
        Sources sources{{"main.mplx", "import \"a.mplx\" as a;import \"b.mplx\" as b;"
                                      "fn main()->i64{return a::value()+b::value();}"},
                        {"a.mplx", "import \"leaf.mplx\" as l;export fn value()->i64{return l::value();}"},
                        {"b.mplx", "import \"leaf.mplx\" as l;export fn value()->i64{return l::value();}"},
                        {"leaf.mplx", "export fn value()->i64{return 21;}"}};
        mplx::WorkspaceCompiler compiler(std::filesystem::current_path(), provider(sources));
        const auto diamond = compiler.build("main.mplx");
        expect(diamond.statistics.parsed == 4 && diamond.dependencies.size() == 4 &&
                   diamond.statistics.linked_functions == 4,
               "diamond dependency is included once");
        expect(execute(diamond, mplx::VM::JitMode::On) == 42, "diamond execution");
        sources["leaf.mplx"] = "import \"main.mplx\" as root;export fn value()->i64{return 21;}";
        const auto cycle = compiler.build("main.mplx");
        expect(std::any_of(cycle.diagnostics.begin(), cycle.diagnostics.end(),
                           [](const auto &d) {
                               return d.path == "leaf.mplx" && d.diagnostic.code == "MPLX4003" &&
                                      d.diagnostic.message.find("main.mplx ->") != std::string::npos;
                           }),
               "cycle chain and offending edge");
        sources.erase("leaf.mplx");
        const auto missing = compiler.build("main.mplx");
        expect(!missing.ok() && std::any_of(missing.diagnostics.begin(), missing.diagnostics.end(),
                                            [](const auto &d) { return d.diagnostic.code == "MPLX4002"; }),
               "deleted dependency must not survive in cache");
        sources["main.mplx"] = "import \"../escape.mplx\" as e;fn main()->i64{return 0;}";
        const auto escape = compiler.build("main.mplx");
        expect(!escape.ok() && escape.diagnostics.front().diagnostic.code == "MPLX4001", "root confinement");
        sources["main.mplx"] = "import \"leaf.mplx\" as l;fn main()->i64{return l::value();}";
        sources["leaf.mplx"] = "export fn value()->i64{return 1/0;}";
        const auto trap = compiler.build("main.mplx");
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
            mplx::VM vm(mplx::CompiledProgram::create(trap.artifact));
            vm.setJitMode(mode);
            try {
                (void)vm.run();
                expect(false, "missing dependency trap");
            } catch (const mplx::RuntimeError &e) {
                expect(e.function() == "leaf.mplx::value" && e.span().end.offset > e.span().start.offset,
                       "dependency trap retains module identity and source span");
            }
        }

        sources.clear();
        for (int i = 0; i < 66; ++i) {
            const auto name = "n" + std::to_string(i) + ".mplx";
            sources[name] = (i < 65 ? "import \"n" + std::to_string(i + 1) + ".mplx\" as next;" : "") +
                            "export fn value()->i64{return 0;}";
        }
        const auto bounded = compiler.build("n0.mplx");
        expect(!bounded.ok() && std::any_of(bounded.diagnostics.begin(), bounded.diagnostics.end(),
                                            [](const auto &d) { return d.diagnostic.code == "MPLX4004"; }),
               "graph depth budget");
        sources.clear();
        std::string fanout;
        for (int i = 0; i < 4097; ++i) {
            fanout += "import \"leaf.mplx\" as n" + std::to_string(i) + ";";
        }
        sources["main.mplx"] = fanout + "fn main()->i64{return 0;}";
        sources["leaf.mplx"] = "fn private_value()->i64{return 0;}";
        const auto edge_budget = compiler.build("main.mplx");
        expect(!edge_budget.ok() && edge_budget.dependencies.size() == 4096 &&
                   std::any_of(edge_budget.diagnostics.begin(), edge_budget.diagnostics.end(),
                               [](const auto &d) { return d.diagnostic.code == "MPLX4004"; }),
               "wide graph edge budget");
        sources.clear();
        for (int i = 0; i < 100; ++i) {
            sources["main.mplx"] += "import \"leaf.mplx\" as n" + std::to_string(i) + ";";
        }
        sources["main.mplx"] += "fn main()->i64{return 0;}";
        sources["leaf.mplx"] = "export fn value(" + std::string(200000, 'x') + ":i64)->i64{return 0;}";
        const auto expanded = compiler.build("main.mplx");
        expect(!expanded.ok() && std::any_of(expanded.diagnostics.begin(), expanded.diagnostics.end(),
                                             [](const auto &d) {
                                                 return d.diagnostic.message.find("metadata budget") !=
                                                        std::string::npos;
                                             }),
               "repeated imports cannot expand parameter names without a budget");
    }
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            return 2;
        }
        const std::string suite = argv[1];
        if (suite == "modules") {
            modules();
        } else if (suite == "incremental") {
            incremental();
        } else if (suite == "dependency-graph") {
            graph_errors();
        } else {
            return 2;
        }
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
