#include "frontend.hpp"
#include "vm.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#if defined(MPLX_WITH_JIT)
#include "jit_compiler.hpp"
#endif

namespace {
    using Clock = std::chrono::steady_clock;
    double milliseconds(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    struct Workload {
        std::string name, source;
        long long expected;
    };
    struct Samples {
        std::string name;
        std::vector<double> milliseconds;
        uint64_t instructions{0};
        uint64_t native_calls{0};
        std::string summary() const {
            auto sorted = milliseconds;
            std::sort(sorted.begin(), sorted.end());
            auto percentile = [&](double fraction) {
                return sorted[static_cast<size_t>(fraction * (sorted.size() - 1))];
            };
            std::string result = "{\"name\":" + mplx::json_quote(name) +
                                 ",\"instructions\":" + std::to_string(instructions) +
                                 ",\"native_calls\":" + std::to_string(native_calls) +
                                 ",\"best_ms\":" + std::to_string(sorted.front()) +
                                 ",\"p50_ms\":" + std::to_string(percentile(.5)) +
                                 ",\"p95_ms\":" + std::to_string(percentile(.95)) +
                                 ",\"worst_ms\":" + std::to_string(sorted.back()) + ",\"samples_ms\":[";
            for (size_t index = 0; index < milliseconds.size(); ++index) {
                if (index)
                    result += ',';
                result += std::to_string(milliseconds[index]);
            }
            return result + "]}";
        }
    };
    std::string benchmark(const Workload &workload, uint32_t runs) {
        const auto start_raw = Clock::now();
        auto raw = mplx::analyze_source(workload.source, false);
        const auto raw_ms = milliseconds(start_raw);
        const auto start_ssa = Clock::now();
        auto optimized = mplx::analyze_source(workload.source, true);
        const auto ssa_ms = milliseconds(start_ssa);
        if (!raw.ok() || !optimized.ok())
            throw std::runtime_error("benchmark source does not compile");
        mplx::VM original(raw.bytecode), machine(optimized.bytecode);
        original.setJitMode(mplx::VM::JitMode::Off);
        machine.setJitMode(mplx::VM::JitMode::Off);
        std::vector<Samples> samples{{"raw-vm", {}, 0}, {"ssa-vm", {}, 0}};
#if defined(MPLX_WITH_JIT)
        auto verified = mplx::VerifiedModule::create(optimized.bytecode);
        mplx::jit::JitCompiler compiler;
        const auto start_arena = Clock::now();
        auto arena = compiler.compileModule(*verified, {0});
        const auto arena_ms = milliseconds(start_arena);
        const auto start_registers = Clock::now();
        auto registers = compiler.compileModule(*verified, {4});
        const auto registers_ms = milliseconds(start_registers);
        const auto raw_verified = mplx::VerifiedModule::create(raw.bytecode);
        const auto start_raw_native = Clock::now();
        auto raw_registers = compiler.compileModule(*raw_verified, {4});
        const auto raw_registers_ms = milliseconds(start_raw_native);
        if (!arena || !registers || !raw_registers)
            throw std::runtime_error("native benchmark unavailable on this target");
        uint32_t entry = UINT32_MAX;
        for (uint32_t index = 0; index < optimized.bytecode.functions.size(); ++index)
            if (optimized.bytecode.functions[index].name == "main")
                entry = index;
        if (entry == UINT32_MAX)
            throw std::runtime_error("missing benchmark main");
        uint32_t raw_entry = UINT32_MAX;
        for (uint32_t index = 0; index < raw.bytecode.functions.size(); ++index)
            if (raw.bytecode.functions[index].name == "main")
                raw_entry = index;
        if (raw_entry == UINT32_MAX)
            throw std::runtime_error("missing raw benchmark main");
        samples.push_back({"ssa-native-arena", {}, 0});
        samples.push_back({"ssa-native-registers", {}, 0});
        samples.push_back({"raw-native-registers", {}, 0});
#endif
        std::vector<size_t> order;
        for (size_t index = 0; index < samples.size(); ++index)
            order.push_back(index);
        std::mt19937 random(0x50455246);
        // Deterministically interleave modes to limit order/thermal bias. Three
        // warmups per mode precede retained samples; invocation allocation counts.
        for (uint32_t iteration = 0; iteration < runs + 3; ++iteration) {
            std::shuffle(order.begin(), order.end(), random);
            for (auto mode : order) {
                const auto start = Clock::now();
                long long value;
                uint64_t instructions;
                uint64_t native_calls = 0;
                if (mode < 2) {
                    auto &vm = mode == 0 ? original : machine;
                    value = vm.run();
                    instructions = vm.stats().instructions;
                } else {
#if defined(MPLX_WITH_JIT)
                    const auto &code = mode == 2 ? *arena : mode == 3 ? *registers : *raw_registers;
                    const auto &bytecode = mode == 4 ? raw.bytecode : optimized.bytecode;
                    const auto function = mode == 4 ? raw_entry : entry;
                    auto execution =
                        mplx::jit::execute_native(bytecode, code, function, 10'000'000, 1024, 1'000'000);
                    if (execution.error != mplx::jit::NativeError::None)
                        throw std::runtime_error("native benchmark trapped");
                    value = execution.value;
                    instructions = execution.instructions;
                    native_calls = execution.native_calls;
#else
                    throw std::runtime_error("invalid benchmark mode");
#endif
                }
                const auto elapsed = milliseconds(start);
                if (value != workload.expected)
                    throw std::runtime_error("benchmark value mismatch in " + workload.name);
                samples[mode].instructions = instructions;
                samples[mode].native_calls = native_calls;
                if (iteration >= 3)
                    samples[mode].milliseconds.push_back(elapsed);
            }
        }
        std::string result = "{\"name\":" + mplx::json_quote(workload.name) +
                             ",\"source\":" + mplx::json_quote(workload.source) +
                             ",\"result\":" + std::to_string(workload.expected) +
                             ",\"raw_compile_ms\":" + std::to_string(raw_ms) +
                             ",\"ssa_compile_ms\":" + std::to_string(ssa_ms) +
                             ",\"passes\":" + mplx::optimization_report_json(optimized.optimization);
#if defined(MPLX_WITH_JIT)
        result += ",\"native\":{\"arena_compile_ms\":" + std::to_string(arena_ms) +
                  ",\"register_compile_ms\":" + std::to_string(registers_ms) +
                  ",\"arena_bytes\":" + std::to_string(arena->size) +
                  ",\"register_bytes\":" + std::to_string(registers->size) +
                  ",\"arena_static_reads\":" + std::to_string(arena->allocation.memory_reads) +
                  ",\"register_static_reads\":" + std::to_string(registers->allocation.memory_reads) +
                  ",\"arena_static_writes\":" + std::to_string(arena->allocation.memory_writes) +
                  ",\"register_static_writes\":" + std::to_string(registers->allocation.memory_writes) +
                  ",\"raw_register_compile_ms\":" + std::to_string(raw_registers_ms) +
                  ",\"raw_register_bytes\":" + std::to_string(raw_registers->size) + "}";
        if (samples[1].instructions != samples[2].instructions ||
            samples[2].instructions != samples[3].instructions)
            throw std::runtime_error("native benchmark instruction accounting mismatch");
        if (samples[0].instructions != samples[4].instructions)
            throw std::runtime_error("raw native benchmark instruction accounting mismatch");
#endif
        result += ",\"execution\":[";
        for (size_t mode = 0; mode < samples.size(); ++mode) {
            if (mode)
                result += ',';
            result += samples[mode].summary();
        }
        return result + "]}";
    }
} // namespace
int main(int argc, char **argv) {
    try {
        const auto runs = argc == 2 ? std::stoul(argv[1]) : 30;
        if (argc > 2 || runs < 3 || runs > 10000)
            throw std::runtime_error("Usage: mplx-compiler-bench [runs:3..10000]");
        const std::vector<Workload> workloads{
            {"numeric-loop", "fn main()->i64{let i=0;let sum=0;while(i<10000){sum=sum+i;i=i+1;}return sum;}",
             49'995'000},
            {"branch-swaps",
             "fn main()->i64{let i=0;let a=2;let b=4;let s=0;while(i<10000){let t=a;a=b;b=t;"
             "if(i%2==0){s=s+a*3-b;}else{s=s+b*3-a;}i=i+1;}return s;}",
             100'000},
            {"small-calls",
             "fn add(a:i64,b:i64)->i64{return a+b;}fn main()->i64{let i=0;let s=0;"
             "while(i<3000){s=add(s,i);i=i+1;}return s;}",
             4'498'500},
            {"recursive-fib",
             "fn fib(n:i64)->i64{if(n<2){return n;}return fib(n-1)+fib(n-2);}"
             "fn main()->i64{return fib(18);}",
             2584},
            {"common-values",
             "fn main()->i64{let i=0;let s=0;while(i<10000){if((i<9999)==(i<9999)){"
             "s=s+i;}i=i+1;}return s;}",
             49'995'000},
            {"proven-total-calls", R"mplx(fn widen(value: i32) -> i64 {
    return i64(value) + 1;
}

fn identity(value: i64) -> i64 {
    return value;
}

fn main() -> i64 {
    let i: i32 = 0;
    while (i < i32(10000)) {
        widen(i);
        identity(i64(i));
        i = i + i32(1);
    }
    return i64(i);
}
)mplx",
             10'000}};
        std::cout << "{\"schema_version\":1,\"version\":\"" MPLX_VERSION
                     "\",\"configuration\":\"" MPLX_BUILD_CONFIGURATION "\",\"runs\":"
                  << runs << ",\"warmup\":3,\"interleaved\":true,\"workloads\":[";
        for (size_t index = 0; index < workloads.size(); ++index) {
            if (index)
                std::cout << ',';
            std::cout << benchmark(workloads[index], static_cast<uint32_t>(runs));
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
