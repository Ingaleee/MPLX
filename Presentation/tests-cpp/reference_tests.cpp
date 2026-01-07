#include "frontend.hpp"
#include "reference.hpp"
#include "vm.hpp"
#include "workspace.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <random>

namespace {
    size_t checks = 0;
    void expect(bool condition, const std::string &message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }
    struct Outcome {
        long long value{0};
        bool arithmetic{false};
        std::string function;
        mplx::SourceSpan span{};
        bool operator==(const Outcome &) const = default;
    };
    Outcome execute(mplx::reference::Program &program, const std::string &entry,
                    std::span<const long long> arguments) {
        try {
            return {program.invoke(entry, arguments), false, {}, {}};
        } catch (const mplx::reference::Error &error) {
            if (error.kind() != mplx::reference::ErrorKind::Arithmetic)
                throw;
            return {0, true, error.function(), error.span()};
        }
    }
    Outcome execute(const mplx::Bytecode &bytecode, mplx::VM::JitMode mode, const std::string &entry,
                    std::span<const long long> arguments) {
        mplx::VM machine(bytecode);
        machine.setJitMode(mode);
        try {
            auto value = machine.run(entry, arguments);
#if defined(MPLX_WITH_JIT)
            if (mode == mplx::VM::JitMode::On)
                expect(machine.stats().jit_executions == 1, "oracle test fell back to interpreter");
#endif
            return {value, false, {}, {}};
        } catch (const mplx::RuntimeError &error) {
            if (error.code() != mplx::RuntimeErrorCode::Arithmetic)
                throw;
            return {0, true, error.function(), error.span()};
        }
    }
    Outcome compare(const std::string &source, const std::string &entry = "main",
                    std::span<const long long> arguments = {}) {
        mplx::reference::Program oracle(source);
        const auto expected = execute(oracle, entry, arguments);
        for (bool optimize : {false, true}) {
            const auto compiled = mplx::analyze_source(source, optimize);
            expect(compiled.ok(), mplx::diagnostics_json(compiled.diagnostics));
            for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
                const auto actual = execute(compiled.bytecode, mode, entry, arguments);
                expect(actual == expected, "AST/bytecode disagreement: " + source +
                                               " expected=" + std::to_string(expected.value) +
                                               " actual=" + std::to_string(actual.value) +
                                               " spans=" + std::to_string(expected.span.start.offset) + ":" +
                                               std::to_string(actual.span.start.offset));
            }
        }
        return expected;
    }
    void semantics() {
        expect(compare("fn main()->i64{let x:i32=2;let y:i32=3;return i64(x*y)+10;}").value == 16,
               "typed reference arithmetic");
        expect(compare("fn main()->bool{return (false && bool(1/0)) || (true || bool(1/0));}").value == 1,
               "short-circuit evaluation");
        expect(compare("fn twice(x:i64)->i64{return x*2;}fn main()->i64{let x=1;let i=0;"
                       "while(i<5){let x=twice(x);i=i+1;}return x;}")
                       .value == 1,
               "loop scope and shadowing");
        expect(compare("fn f(x:i64)->i64{if(x>2){return x;}return f(x+1)+1;}"
                       "fn main()->i64{return f(0);}")
                       .value == 6,
               "explicit continuation returns");
        expect(compare("fn main()->i32{let x:i32=2147483647;return x+i32(1);}").arithmetic,
               "i32 arithmetic overflow");
        expect(compare("fn main()->i64{return 9223372036854775807+1;}").arithmetic, "i64 overflow");
        expect(compare("fn main()->i64{return (-9223372036854775807-1)/-1;}").arithmetic,
               "division overflow");
        expect(compare("fn main()->i64{return (-9223372036854775807-1)%-1;}").arithmetic,
               "remainder overflow");
        expect(compare("fn main()->i64{return -(-9223372036854775807-1);}").arithmetic, "negation overflow");
        expect(compare("fn main()->i64{return 4/0;}").arithmetic, "division zero");
        expect(compare("fn main()->i64{return 4%0;}").arithmetic, "remainder zero");
        expect(compare("fn main()->i32{return 2147483648;}").arithmetic, "return conversion");
        expect(compare("fn f(a:i32,b:i64)->i64{return b;}"
                       "fn main()->i64{return f(2147483648,1/0);}")
                   .arithmetic,
               "argument conversion must precede later argument trap");
        expect(compare("fn f(a:i64,b:i32)->i64{return a;}"
                       "fn main()->i64{return f(1/0,2147483648);}")
                   .arithmetic,
               "argument evaluation order");
        expect(compare("fn main()->i64{let x:i32=1;x=2147483648;return 0;}").arithmetic,
               "discarded assignment trap survives optimization");
        expect(compare("fn main()->i64{i32(2147483648);return 0;}").arithmetic,
               "discarded cast trap survives optimization");
        std::array<long long, 3> arguments{-7, 1, 9};
        expect(
            compare("fn typed(a:i32,b:bool,c:i64)->i64{if(b){return i64(a)+c;}return 0;}", "typed", arguments)
                    .value == 2,
            "typed host carriers");
        compare("fn main()->i64{}", "main");
    }
    void generated() {
        std::mt19937 generator(0x41535437);
        for (unsigned sample = 0; sample < 600; ++sample) {
            const auto count = generator() % 17;
            const auto divisor = 1 + generator() % 7;
            const auto a = static_cast<int>(generator() % 200) - 100;
            const auto b = static_cast<int>(generator() % 200) - 100;
            const auto source =
                "fn adjust(x:i64,y:i64)->i64{if(x<y){return x+y;}return x-y;}"
                "fn main()->i64{let a=" +
                std::to_string(a) + ";let b=" + std::to_string(b) + ";let sum:i32=0;let i=0;while(i<" +
                std::to_string(count) + "){let old=a;a=b;b=adjust(old,b)%" + std::to_string(divisor) +
                ";if((i%2==0)&&a!=b){sum=sum+i32(a-b);}else{sum=sum+i32(b);}i=i+1;}return sum;}";
            compare(source);
        }
        constexpr std::array<long long, 9> extremes{INT64_MIN, INT64_MIN + 1, INT32_MIN,     -1,       0,
                                                    1,         INT32_MAX,     INT64_MAX - 1, INT64_MAX};
        for (const auto &operation : {"+", "-", "*", "/", "%", "<", "==", "!="}) {
            const auto return_type =
                std::string(operation == std::string("<") || operation == std::string("==") ||
                                    operation == std::string("!=")
                                ? "bool"
                                : "i64");
            const auto source = "fn calculate(a:i64,b:i64)->" + return_type + "{return a" + operation + "b;}";
            for (auto a : extremes)
                for (auto b : extremes) {
                    std::array<long long, 2> arguments{a, b};
                    compare(source, "calculate", arguments);
                }
        }
    }
    void modules() {
        const std::map<std::string, std::string> sources{
            {"main.mplx", "import \"lib/math.mplx\" as math;fn main()->i64{return math::total(8);}"},
            {"lib/math.mplx", "import \"../shared.mplx\" as shared;export fn total(x:i64)->i64{"
                              "if(x==0){return 0;}return shared::double(x)+total(x-1);}"},
            {"shared.mplx", "export fn double(x:i64)->i64{return x*2;}"}};
        mplx::reference::Program oracle(sources, "main.mplx");
        expect(oracle.invoke() == 72, "reference relative imports and recursive call");
        mplx::WorkspaceCompiler compiler(
            std::filesystem::current_path(), [&](const std::string &path) -> std::optional<std::string> {
                auto found = sources.find(path);
                return found == sources.end() ? std::nullopt : std::optional(found->second);
            });
        auto result = compiler.build("main.mplx");
        expect(result.ok(), mplx::workspace_diagnostics_json(result));
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On})
            expect(execute(result.artifact->bytecode(), mode, "main", {}).value == oracle.invoke(),
                   "reference module link disagreement");
    }
    void bounds() {
        using namespace mplx::reference;
        auto error = [](Program &program, ErrorKind kind, Limits limits, const std::string &entry = "main",
                        std::span<const long long> arguments = {}) {
            try {
                program.invoke(entry, arguments, limits);
                expect(false, "missing reference limit");
            } catch (const Error &failure) {
                expect(failure.kind() == kind, "wrong reference limit");
            }
        };
        Program program("fn descend(n:i64)->i64{if(n==0){return 7;}return descend(n-1);}"
                        "fn spin()->i64{while(true){}return 0;}"
                        "fn typed(x:i32,b:bool)->i64{return x;}fn main()->i64{return 42;}");
        error(program, ErrorKind::StepLimit, {50, 8, 100}, "spin");
        expect(program.invoke() == 42, "reference reuse after step error");
        std::array<long long, 1> depth{6000};
        expect(program.invoke("descend", depth, {500'000, 6001, 100'000}) == 7,
               "reference language recursion uses heap continuations");
        error(program, ErrorKind::FrameLimit, {500'000, 32, 100'000}, "descend", depth);
        error(program, ErrorKind::ValueLimit, {500'000, 6001, 10}, "descend", depth);
        error(program, ErrorKind::FrameLimit, {100, 0, 100});
        error(program, ErrorKind::ContinuationLimit, {100, 10, 100, 0});
        Program early("fn main()->i64{return 42;main();main();main();}");
        expect(early.invoke("main", {}, {100, 10, 100, 10}) == 42,
               "early return releases pending continuations");
        expect(early.invoke("main", {}, {100, 10, 100, 10}) == 42,
               "continuation accounting is reset for reuse");
        Program wide(
            "fn recurse()->i64{recurse();recurse();recurse();return 0;}fn main()->i64{return recurse();}");
        error(wide, ErrorKind::ContinuationLimit, {100000, 10000, 100000, 20});
        expect(program.invoke() == 42, "reuse after continuation bound failure");
        std::array<long long, 2> invalid_bool{1, 2}, invalid_i32{INT64_MAX, 0};
        error(program, ErrorKind::InvalidEntry, {}, "typed", invalid_bool);
        error(program, ErrorKind::InvalidEntry, {}, "typed", invalid_i32);
        expect(program.invoke() == 42, "reference reuse after frame/value errors");
    }
} // namespace
int main() {
    try {
        semantics();
        generated();
        modules();
        bounds();
        std::cout << "Independent AST oracle: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
