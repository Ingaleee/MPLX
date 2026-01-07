#include "module_format.hpp"
#include "vm.hpp"
#include <cstdlib>
#include <tuple>

namespace {
    struct Outcome {
        long long value{0};
        int error{-1};
        std::string function;
        uint32_t pc{0};
        mplx::SourceSpan span{};
        uint64_t instructions{0}, operands{0}, locals{0}, frames{0};
        bool operator==(const Outcome &other) const {
            return std::tie(value, error, function, pc, instructions, operands, locals, frames) ==
                       std::tie(other.value, other.error, other.function, other.pc, other.instructions,
                                other.operands, other.locals, other.frames) &&
                   span.start.offset == other.span.start.offset && span.end.offset == other.span.end.offset;
        }
    };
    Outcome run(const std::shared_ptr<const mplx::CompiledProgram> &program, mplx::VM::JitMode mode) {
        mplx::VM vm(program);
        vm.setJitMode(mode);
        vm.setLimits({1000, 16, 1024});
        Outcome outcome;
        try {
            outcome.value =
                vm.runByIndex(0, std::vector<long long>(program->module().bytecode().functions[0].arity, 0));
        } catch (const mplx::RuntimeError &error) {
            outcome.error = static_cast<int>(error.code());
            outcome.function = error.function();
            outcome.pc = error.pc();
            outcome.span = error.span();
        }
        const auto &s = vm.stats();
        outcome.instructions = s.instructions;
        outcome.operands = s.peak_operands;
        outcome.locals = s.peak_locals;
        outcome.frames = s.peak_frames;
        return outcome;
    }
    void check(std::span<const uint8_t> bytes) {
        std::shared_ptr<const mplx::VerifiedModule> module;
        try {
            module = mplx::deserialize_module(bytes);
        } catch (const mplx::ModuleFormatError &) {
            return;
        }
        if (mplx::serialize_module(*module) != std::vector<uint8_t>(bytes.begin(), bytes.end()))
            std::abort();
        const auto program = mplx::CompiledProgram::create(module);
        const auto reference = run(program, mplx::VM::JitMode::Off);
#if defined(MPLX_WITH_JIT)
        if (!(reference == run(program, mplx::VM::JitMode::On)))
            std::abort();
#else
        (void)reference;
#endif
    }
    void put(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
        for (size_t b = 0; b < 4; ++b)
            bytes[offset + b] = static_cast<uint8_t>(value >> (8 * b));
    }
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 256 || size > 16384)
        return 0;
    check({data, size});
    // Exercise directory/metadata/bytecode validation past the checksum gate.
    std::vector<uint8_t> repaired(data, data + size);
    put(repaired, 48, mplx::crc32c(std::span(repaired).subspan(64)));
    put(repaired, 52, 0);
    put(repaired, 52, mplx::crc32c(std::span(repaired).first(64)));
    check(repaired);
    return 0;
}
