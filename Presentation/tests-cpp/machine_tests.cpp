#include "frontend.hpp"
#include "vm.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#if defined(MPLX_WITH_JIT)
#include "jit_compiler.hpp"
#include "machine_ir.hpp"
#endif

namespace {
    size_t checks = 0;
    void expect(bool condition, const std::string &message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }
#if defined(MPLX_WITH_JIT)
    void compare(const std::string &source, const std::string &entry = "main",
                 std::span<const long long> arguments = {}, mplx::ExecutionLimits limits = {}) {
        auto analysis = mplx::analyze_source(source);
        expect(analysis.ok(), mplx::diagnostics_json(analysis.diagnostics));
        auto module = mplx::VerifiedModule::create(analysis.bytecode);
        auto machine = mplx::jit::lower_machine_ir(*module);
        expect(!machine.blocks.empty(), "missing native basic blocks");
        for (size_t value = 0; value < machine.values.size(); ++value) {
            const auto &definition = machine.values[value];
            expect(definition.block < machine.blocks.size(), "machine value has invalid block");
            expect(std::is_sorted(definition.uses.begin(), definition.uses.end()), "unordered native uses");
            for (auto use : definition.uses)
                expect(definition.definition == mplx::jit::no_machine_value || definition.definition <= use,
                       "machine value used before its definition");
        }
        uint32_t function = UINT32_MAX;
        for (uint32_t index = 0; index < analysis.bytecode.functions.size(); ++index)
            if (analysis.bytecode.functions[index].name == entry)
                function = index;
        expect(function != UINT32_MAX, "missing native entry");
        mplx::jit::JitCompiler compiler;
        auto baseline = compiler.compileModule(*module, {0});
        expect(baseline.has_value(), "arena baseline did not compile");
        auto expected = mplx::jit::execute_native(analysis.bytecode, *baseline, function, limits.instructions,
                                                  limits.frames, limits.values, arguments);
        mplx::VM vm(analysis.bytecode);
        vm.setJitMode(mplx::VM::JitMode::Off);
        vm.setLimits(limits);
        try {
            const auto value = vm.run(entry, arguments);
            expect(expected.error == mplx::jit::NativeError::None && expected.value == value,
                   "baseline native/VM value mismatch");
        } catch (const mplx::RuntimeError &error) {
            expect(expected.error != mplx::jit::NativeError::None, "baseline native missed VM trap");
            expect(expected.pc == error.pc(), "baseline native trap PC mismatch");
        }
        expect(expected.instructions == vm.stats().instructions, "baseline instruction accounting");
        for (uint32_t registers : {1, 2, 3, 4}) {
            auto compiled = compiler.compileModule(*module, {registers});
            expect(compiled.has_value(), "register allocation failed to compile");
            auto actual =
                mplx::jit::execute_native(analysis.bytecode, *compiled, function, limits.instructions,
                                          limits.frames, limits.values, arguments);
            expect(actual == expected, "register allocation changed observable execution");
            expect(compiled->allocation.register_writes > 0, "register allocation not exercised");
        }
    }
    void pressure() {
        const auto source =
            "fn mix(a:i64,b:i64,c:i64,d:i64,e:i64,f:i64,g:i64,h:i64)->i64{"
            "return (a+b)*((c+d)*(e+f))+(g+h);}fn identity(x:i64)->i64{return x;}"
            "fn main()->i64{let a=identity(1);let b=identity(2);let c=identity(3);let d=identity(4);"
            "let e=identity(5);let f=identity(6);let g=identity(7);let h=identity(8);return "
            "mix(a,b,c,d,e,f,g,h);}";
        compare(source);
        auto result = mplx::analyze_source(source);
        auto verified = mplx::VerifiedModule::create(result.bytecode);
        mplx::jit::JitCompiler compiler;
        auto arena = compiler.compileModule(*verified, {0});
        auto registers = compiler.compileModule(*verified, {4});
        auto one = compiler.compileModule(*verified, {1});
        expect(one->allocation.spills > 0, "pressure fixture never spilled");
        expect(registers->allocation.memory_reads < arena->allocation.memory_reads,
               "native cache did not reduce arena reads");
        expect(registers->allocation.coalesced_copies > 0, "native copy coalescing never exercised");
        std::cout << "Arena reads: " << arena->allocation.memory_reads << " -> "
                  << registers->allocation.memory_reads << "; one-register spills: " << one->allocation.spills
                  << '\n';
        try {
            compiler.compileModule(*verified, {5});
            expect(false, "unsupported register count accepted");
        } catch (const std::invalid_argument &) {
        }
        compare("fn walk(n:i64)->i64{if(n==0){return 7;}return walk(n-1)+1;}"
                "fn main()->i64{return walk(6000);}",
                "main", {}, {1'000'000, 6002, 500'000});
        compare("fn recurse(n:i64)->i64{return recurse(n+1);}fn main()->i64{return recurse(0);}", "main", {},
                {10000, 32, 10000});
        compare(source, "main", {}, {11, 32, 10000});
        compare(source, "main", {}, {10000, 32, 3});
        compare("fn main()->i32{return 2147483648;}");
        compare("fn main()->i64{return (-9223372036854775807-1)/-1;}");
        compare("fn main()->i64{return 8/0;}");
        compare("fn main()->i64{let a=1;let b=2;let c=3;let i=0;while(i<11){"
                "let t=a;a=b;b=c;c=t;i=i+1;}return a*100+b*10+c;}");
    }
    void generated() {
        std::mt19937 generator(0x52454737);
        const auto source = "fn next(a:i64,b:i64)->i64{if(a<b){return (a+b)%17;}return (a-b)%13;}"
                            "fn main(a:i64,b:i64,n:i64)->i64{let i=0;let s=0;while(i<n){"
                            "let old=a;a=b;b=next(old,b);s=s+a*3-b*2;i=i+1;}return s;}";
        for (int sample = 0; sample < 150; ++sample) {
            std::array<long long, 3> arguments{static_cast<long long>(generator() % 100),
                                               static_cast<long long>(generator() % 100),
                                               static_cast<long long>(generator() % 30)};
            compare(source, "main", arguments);
        }
    }
#endif
} // namespace
int main() {
    try {
#if defined(MPLX_WITH_JIT)
        pressure();
        generated();
        std::cout << "Native register allocator: " << checks << " checks passed\n";
#else
        std::cout << "Native backend disabled; machine tests skipped\n";
#endif
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
