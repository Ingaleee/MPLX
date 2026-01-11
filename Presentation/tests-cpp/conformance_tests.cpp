#include "../../conformance/generated_cases.hpp"
#include "frontend.hpp"
#include "reference.hpp"
#include "vm.hpp"
#include <algorithm>
#include <array>
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
        std::string kind{"value"}, function;
        long long value{0};
        uint32_t start{0}, end{0};
        bool operator==(const Outcome &) const = default;
    };
    Outcome execute(mplx::reference::Program &program, const std::string &entry,
                    std::span<const long long> arguments) {
        try {
            return {"value", {}, program.invoke(entry, arguments)};
        } catch (const mplx::reference::Error &error) {
            if (error.kind() == mplx::reference::ErrorKind::InvalidEntry)
                return {"invalid-entry", {}, 0, 0, 0};
            if (error.kind() != mplx::reference::ErrorKind::Arithmetic)
                throw;
            return {"arithmetic", error.function(), 0, error.span().start.offset, error.span().end.offset};
        }
    }
    Outcome execute(const mplx::Bytecode &bytecode, mplx::VM::JitMode mode, const std::string &entry,
                    std::span<const long long> arguments) {
        mplx::VM vm(bytecode);
        vm.setJitMode(mode);
        try {
            const auto value = vm.run(entry, arguments);
#if defined(MPLX_WITH_JIT)
            if (mode == mplx::VM::JitMode::On)
                expect(vm.stats().jit_executions == 1, "conformance native execution fell back");
#endif
            return {"value", {}, value};
        } catch (const mplx::RuntimeError &error) {
            if (error.code() == mplx::RuntimeErrorCode::InvalidEntry)
                return {"invalid-entry", {}, 0, 0, 0};
            if (error.code() != mplx::RuntimeErrorCode::Arithmetic)
                throw;
            return {"arithmetic", error.function(), 0, error.span().start.offset, error.span().end.offset};
        }
    }
    void compare_bytecode(const mplx::Bytecode &bytecode, const Outcome &expected, const std::string &label,
                          const std::string &entry, std::span<const long long> arguments) {
        expect(execute(bytecode, mplx::VM::JitMode::Off, entry, arguments) == expected,
               label + " VM outcome");
        expect(execute(bytecode, mplx::VM::JitMode::On, entry, arguments) == expected,
               label + " JIT outcome");
#if defined(MPLX_WITH_JIT)
        if (expected.kind == "invalid-entry")
            return; // Host carrier validation is at the VM/embedding boundary.
        const auto module = mplx::VerifiedModule::create(bytecode);
        auto found = std::find_if(bytecode.functions.begin(), bytecode.functions.end(),
                                  [&](const auto &function) { return function.name == entry; });
        expect(found != bytecode.functions.end(), label + " entry identity");
        const auto index = static_cast<uint32_t>(found - bytecode.functions.begin());
        mplx::jit::JitCompiler compiler;
        auto arena = compiler.compileModule(*module, {0});
        auto registers = compiler.compileModule(*module, {4});
        expect(arena.has_value() && registers.has_value(), label + " native compilation");
        const auto baseline =
            mplx::jit::execute_native(bytecode, *arena, index, 1'000'000, 1024, 1'000'000, arguments);
        const auto allocated =
            mplx::jit::execute_native(bytecode, *registers, index, 1'000'000, 1024, 1'000'000, arguments);
        expect(baseline == allocated, label + " native allocation changed full outcome");
        if (expected.kind == "value")
            expect(baseline.error == mplx::jit::NativeError::None && baseline.value == expected.value,
                   label + " native value");
        else {
            expect(baseline.error == mplx::jit::NativeError::Overflow ||
                       baseline.error == mplx::jit::NativeError::DivisionByZero ||
                       baseline.error == mplx::jit::NativeError::I32Range,
                   label + " native trap category");
            expect(baseline.function < bytecode.functions.size() &&
                       bytecode.functions[baseline.function].name == expected.function,
                   label + " native trap function");
            const auto mapping = std::find_if(bytecode.source_map.begin(), bytecode.source_map.end(),
                                              [&](const auto &item) { return item.offset == baseline.pc; });
            expect(mapping != bytecode.source_map.end(), label + " native source PC");
            const auto span = mapping->span;
            expect(span.start.offset == expected.start && span.end.offset == expected.end,
                   label + " native trap span");
        }
#endif
    }
    void manifest_cases() {
        for (const auto &test : mplx::conformance::cases) {
            auto compiled = mplx::analyze_source(test.source, false);
            if (test.kind == "diagnostic") {
                for (bool optimized : {false, true}) {
                    auto invalid = mplx::analyze_source(test.source, optimized);
                    expect(std::any_of(
                               invalid.diagnostics.begin(), invalid.diagnostics.end(),
                               [&](const auto &diagnostic) { return diagnostic.code == test.diagnostic; }),
                           test.id + " missing diagnostic " + test.diagnostic);
                }
                continue;
            }
            expect(compiled.ok(), test.id + ": " + mplx::diagnostics_json(compiled.diagnostics));
            const Outcome expected{test.kind, test.function, test.value, test.fault_start, test.fault_end};
            mplx::reference::Program oracle(test.source);
            expect(execute(oracle, test.entry, test.arguments) == expected,
                   test.id + " AST violates normative outcome");
            compare_bytecode(compiled.bytecode, expected, test.id + " raw", test.entry, test.arguments);
            const auto report = mplx::run_optimization_pipeline(compiled.ir, {true});
            for (const auto &pass : report.passes) {
                auto intermediate = mplx::deserialize_ir_snapshot(pass.after_snapshot);
                const auto bytecode = mplx::Compiler(false).compile(intermediate);
                expect(bytecode.diags.empty(), test.id + " intermediate lowering");
                compare_bytecode(bytecode.bc, expected, test.id + " after " + pass.name, test.entry,
                                 test.arguments);
                auto replay = mplx::deserialize_ir_snapshot(pass.before_snapshot);
                mplx::run_optimization_pipeline(replay, {}, pass.name);
                expect(mplx::serialize_ir_snapshot(replay) == pass.after_snapshot,
                       test.id + " pass replay differs");
            }
        }
    }
    Outcome compare_program(const std::string &source, long long argument) {
        const std::array<long long, 1> arguments{argument};
        mplx::reference::Program oracle(source);
        const auto expected = execute(oracle, "main", arguments);
        for (bool optimize : {false, true}) {
            const auto result = mplx::analyze_source(source, optimize);
            expect(result.ok(), mplx::diagnostics_json(result.diagnostics));
            compare_bytecode(result.bytecode, expected, "metamorphic", "main", arguments);
        }
        return expected;
    }
    void metamorphic_cases() {
        std::mt19937 random(0x4d504c58);
        for (unsigned index = 0; index < 120; ++index) {
            const auto first = static_cast<long long>(random() % 101) - 50;
            const auto second = static_cast<long long>(random() % 101) - 50;
            const auto iterations = random() % 15;
            const auto argument = static_cast<long long>(random() % 101) - 50;
            const std::string prefix = "fn identity(x: i64) -> i64 {\n    return x;\n}\n";
            const std::string body =
                "    let a = " + std::to_string(first) + ";\n" + "    let b = " + std::to_string(second) +
                ";\n" + "    let i = 0;\n    while (i < " + std::to_string(iterations) + ") {\n" +
                "        let t = a;\n        a = b;\n        b = t;\n        i = i + 1;\n    }\n" +
                "    if (n < 0) {\n        return identity(a) - n;\n    }\n    return identity(b) + n;\n";
            const auto original = prefix + "fn main(n: i64) -> i64 {\n" + body + "}\n";
            const auto expected = compare_program(original, argument);
            const auto swapped_first = iterations % 2 ? second : first;
            const auto swapped_second = iterations % 2 ? first : second;
            expect(expected.kind == "value" && expected.value == (argument < 0 ? swapped_first - argument
                                                                               : swapped_second + argument),
                   "generated loop mathematical oracle");
            // Fresh lexical names, a total unused call, and an unreachable checked
            // trap each exercise a different legal source transformation.
            auto renamed = original;
            size_t position = 0;
            while ((position = renamed.find("identity", position)) != std::string::npos) {
                renamed.replace(position, 8, "renamed_identity");
                position += 16;
            }
            expect(compare_program(renamed, argument) == expected, "alpha-renaming changed semantics");
            const auto dead_total = prefix + "fn main(n: i64) -> i64 {\n    identity(17);\n" + body + "}\n";
            expect(compare_program(dead_total, argument) == expected, "total unused call changed semantics");
            const auto dead_branch =
                prefix + "fn main(n: i64) -> i64 {\n    if (false) {\n        return 7 / 0;\n    }\n" + body +
                "}\n";
            expect(compare_program(dead_branch, argument) == expected, "unreachable trap changed semantics");
        }
    }
} // namespace
int main() {
    try {
        manifest_cases();
        metamorphic_cases();
        std::cout << "Semantics v1: " << mplx::conformance::cases.size() << " normative cases; " << checks
                  << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
