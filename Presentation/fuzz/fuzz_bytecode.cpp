#include "verifier.hpp"
#include "vm.hpp"
#include <cstdlib>
namespace {
    struct Outcome {
        long long value{0};
        int error{-1};
        uint32_t pc{0};
        uint64_t instructions{0};
        std::string function;
        bool operator==(const Outcome &) const = default;
    };
    Outcome execute(const mplx::Bytecode &bytecode, mplx::VM::JitMode mode) {
        mplx::VM vm(bytecode);
        vm.setJitMode(mode);
        vm.setLimits({1000, 16, 1024});
        Outcome result;
        try {
            result.value = vm.run();
        } catch (const mplx::RuntimeError &error) {
            result.error = static_cast<int>(error.code());
            result.pc = error.pc();
            result.function = error.function();
        }
        result.instructions = vm.stats().instructions;
        return result;
    }
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > 8192)
        return 0;
    mplx::Bytecode bc;
    bc.consts = {0, 1, -1, 9223372036854775807LL};
    bc.functions = {{"main", 0, 0, static_cast<uint16_t>(data[0] % 32)}};
    bc.code.assign(data + 1, data + size);
    auto verified = mplx::verify_bytecode(bc);
    if (verified.ok()) {
        const auto reference = execute(bc, mplx::VM::JitMode::Off);
#if defined(MPLX_WITH_JIT)
        if (!(reference == execute(bc, mplx::VM::JitMode::On)))
            std::abort();
#else
        (void)reference;
#endif
    }
    return 0;
}
