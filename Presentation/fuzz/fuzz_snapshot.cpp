#include "compiler.hpp"
#include "ir_passes.hpp"
#include "vm.hpp"
#include <cstdlib>

namespace {
    struct Outcome {
        long long value{0};
        int error{-1};
        std::string function;
        mplx::SourceSpan span;
        uint64_t instructions{0};
    };
    Outcome execute(const mplx::Bytecode &bytecode, mplx::VM::JitMode mode, uint32_t entry) {
        mplx::VM vm(bytecode);
        vm.setJitMode(mode);
        vm.setLimits({2000, 16, 4096});
        Outcome result;
        try {
            result.value = vm.runByIndex(entry, std::vector<long long>(bytecode.functions[entry].arity, 0));
        } catch (const mplx::RuntimeError &error) {
            result.error = static_cast<int>(error.code());
            result.function = error.function();
            result.span = error.span();
        }
        result.instructions = vm.stats().instructions;
        return result;
    }
    bool completed(const Outcome &value) {
        return value.error == -1 || value.error == static_cast<int>(mplx::RuntimeErrorCode::Arithmetic);
    }
    bool equivalent(const Outcome &first, const Outcome &second) {
        return first.value == second.value && first.error == second.error &&
               first.function == second.function && first.span == second.span;
    }
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 32768)
        return 0;
    const std::string_view bytes(reinterpret_cast<const char *>(data), size);
    mplx::IRModule module;
    try {
        module = mplx::deserialize_ir_snapshot(bytes);
    } catch (const std::exception &) {
        return 0;
    }
    if (mplx::serialize_ir_snapshot(module) != bytes || !mplx::verify_ir(module).empty())
        std::abort();
    const auto raw = mplx::Compiler(false).compile(module);
    if (!raw.diags.empty())
        std::abort();
    // Every accepted snapshot goes through the real production optimizer.
    mplx::run_optimization_pipeline(module);
    const auto optimized = mplx::Compiler(false).compile(module);
    if (!optimized.diags.empty())
        std::abort();
    const auto first = execute(raw.bc, mplx::VM::JitMode::Off, 0);
    const auto second = execute(optimized.bc, mplx::VM::JitMode::Off, 0);
    if (completed(first) && completed(second) && !equivalent(first, second))
        std::abort();
#if defined(MPLX_WITH_JIT)
    const auto native = execute(optimized.bc, mplx::VM::JitMode::On, 0);
    if (!equivalent(second, native) || second.instructions != native.instructions)
        std::abort();
#endif
    return 0;
}
