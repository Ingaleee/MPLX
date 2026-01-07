#include "frontend.hpp"
#include "ir_passes.hpp"
#include "ssa.hpp"
#include "vm.hpp"
#include <cstdlib>

namespace {
    struct Outcome {
        long long value{0};
        int error{-1};
        std::string function;
        mplx::SourceSpan span;
        bool operator==(const Outcome &) const = default;
    };
    Outcome execute(const mplx::Bytecode &bytecode, mplx::VM::JitMode mode) {
        mplx::VM vm(bytecode);
        vm.setJitMode(mode);
        vm.setLimits({2000, 16, 4096});
        Outcome result;
        try {
            result.value = vm.run();
        } catch (const mplx::RuntimeError &error) {
            result.error = static_cast<int>(error.code());
            result.function = error.function();
            result.span = error.span();
        }
        return result;
    }
    bool completed(const Outcome &value) {
        return value.error == -1 || value.error == static_cast<int>(mplx::RuntimeErrorCode::Arithmetic);
    }
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 2048)
        return 0;
    auto result = mplx::analyze_source("fn main()->i64{let a=2;let b=4;let i=0;while(i<5){"
                                       "let t=a;a=b;b=t;i=i+1;}if(a>b){return a*10+b;}return a+b;}",
                                       false);
    if (!result.ok())
        std::abort();
    auto module = std::move(result.ir);
    if (data[0] & 1)
        mplx::promote_locals_to_ssa(module);
    auto &function = module.functions[0];
    for (size_t position = 1; position + 3 < size; position += 4) {
        auto &block = function.blocks[data[position] % function.blocks.size()];
        const auto value = data[position + 3];
        if (data[position + 1] % 8 == 0) {
            block.terminator.first = value % (function.blocks.size() + 1);
        } else if (data[position + 1] % 8 == 1) {
            block.terminator.value = value % (function.values.size() + 1);
        } else if (!block.instructions.empty()) {
            auto &instruction = block.instructions[data[position + 2] % block.instructions.size()];
            switch (data[position + 1] % 8) {
            case 2:
                instruction.constant = static_cast<int8_t>(value);
                break;
            case 3:
                instruction.index = value;
                break;
            case 4:
                instruction.result = value % (function.values.size() + 1);
                break;
            case 5:
                if (!instruction.arguments.empty())
                    instruction.arguments[0] = value % (function.values.size() + 1);
                break;
            case 6:
                if (!instruction.incoming_blocks.empty())
                    instruction.incoming_blocks[0] = value % (function.blocks.size() + 1);
                break;
            default:
                instruction.type = static_cast<mplx::ValueType>(value % 5);
                break;
            }
        }
    }
    if (!mplx::verify_ir(module).empty())
        return 0;
    const auto original = mplx::Compiler(false).compile(module);
    if (!original.diags.empty())
        std::abort();
    auto optimized = module;
    mplx::run_optimization_pipeline(optimized);
    const auto lowered = mplx::Compiler(false).compile(optimized);
    if (!lowered.diags.empty())
        std::abort();
    const auto reference = execute(original.bc, mplx::VM::JitMode::Off);
    const auto candidate = execute(lowered.bc, mplx::VM::JitMode::Off);
    if (completed(reference) && completed(candidate) && !(reference == candidate))
        std::abort();
#if defined(MPLX_WITH_JIT)
    if (!(candidate == execute(lowered.bc, mplx::VM::JitMode::On)))
        std::abort();
#endif
    return 0;
}
