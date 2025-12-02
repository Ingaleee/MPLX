#pragma once
#include "../mplx-compiler/verified_module.hpp"
#include "platform.hpp"
#include "machine_ir.hpp"
#include <cstdint>
#include <optional>
#include <span>
namespace mplx::jit {
    enum class NativeError : uint32_t {
        None,
        InstructionLimit,
        Overflow,
        DivisionByZero,
        FrameLimit,
        ValueLimit,
        Internal,
        I32Range
    };
    struct ExecutionContext {
        uint64_t remaining{0};
        uint32_t error{0}, pc{0}, function{0}, reserved{0};
        long long result{0};
        void *state{nullptr};
        long long *active_base{nullptr};
        void *entry{nullptr};
        uint64_t value_limit{0}, local_values{0}, operand_prefix{0};
        uint64_t peak_operands{0}, peak_locals{0}, peak_frames{0}, frame_limit{0}, depth{0}, native_calls{0};
    };
    using JitEntryPtr = long long (*)(ExecutionContext *);
    struct JitCompiled {
        plat::ExecutableMemory memory;
        size_t size{0};
        JitEntryPtr entry{nullptr};
        std::vector<void *> functions;
        std::vector<VerifiedFunction> metadata;
        AllocationStats allocation;
    };
    class JitCompiler {
      public:
        std::optional<JitCompiled> compileModule(const VerifiedModule &module, NativeOptions options = {});
    };
    struct NativeResult {
        long long value{0};
        NativeError error{NativeError::None};
        uint32_t pc{0}, function{0};
        uint64_t instructions{0}, peak_operands{0}, peak_locals{0}, peak_frames{0}, native_calls{0};
        bool operator==(const NativeResult &) const = default;
    };
    NativeResult execute_native(const Bytecode &bytecode, const JitCompiled &module, uint32_t entry,
                                uint64_t instructions, uint64_t frames, uint64_t values,
                                std::span<const long long> arguments = {});
    extern "C" void *native_enter(ExecutionContext *context, uint32_t function, uint32_t caller_height,
                                  void *continuation) noexcept;
    extern "C" void *native_return(ExecutionContext *context, long long value) noexcept;
} // namespace mplx::jit
