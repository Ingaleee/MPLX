#pragma once
#include "bytecode.hpp"
#include "program.hpp"
#include "verifier.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(MPLX_WITH_JIT)
#include "../Jit/jit_compiler.hpp"
#endif

namespace mplx {
    enum class RuntimeErrorCode {
        InvalidEntry,
        InstructionLimit,
        FrameLimit,
        ValueLimit,
        Cancelled,
        Arithmetic,
        InvalidState
    };
    class RuntimeError : public std::runtime_error {
      public:
        RuntimeError(RuntimeErrorCode code, std::string message, std::string function, uint32_t pc,
                     SourceSpan span = {});
        RuntimeErrorCode code() const {
            return code_;
        }
        const std::string &function() const {
            return function_;
        }
        uint32_t pc() const {
            return pc_;
        }
        SourceSpan span() const {
            return span_;
        }

      private:
        RuntimeErrorCode code_;
        std::string function_;
        uint32_t pc_;
        SourceSpan span_;
    };
    struct ExecutionLimits {
        uint64_t instructions{10'000'000};
        size_t frames{1024}, values{1'000'000};
    };
    struct ExecutionStats {
        uint64_t instructions{0}, peak_operands{0}, peak_locals{0}, peak_frames{0};
        uint64_t jit_compilations{0}, jit_executions{0}, jit_fallbacks{0};
        uint64_t native_calls{0};
    };
    class VM {
      public:
        // Own a snapshot: verification cannot be invalidated by caller mutation.
        explicit VM(Bytecode bc);
        explicit VM(std::shared_ptr<const CompiledProgram> program);
        VM(const VM &) = delete;
        VM &operator=(const VM &) = delete;
        long long run(const std::string &entry = "main", std::span<const long long> arguments = {});
        long long runByIndex(uint32_t fnIndex, std::span<const long long> arguments = {});
        enum class JitMode { Off, On, Auto };
        void setJitMode(JitMode mode) {
            jit_mode_ = mode;
        }
        JitMode jitMode() const {
            return jit_mode_;
        }
        void setHotThreshold(uint32_t value) {
            hot_threshold_ = value ? value : 1;
        }
        void setTrace(bool enabled) {
            trace_enabled_ = enabled;
        }
        void setTraceLimit(uint64_t limit) {
            trace_limit_ = limit;
        }
        bool isTraceEnabled() const {
            return trace_enabled_;
        }
        uint64_t traceLimit() const {
            return trace_limit_;
        }
        void setLimits(ExecutionLimits limits) {
            limits_ = limits;
        }
        void setCancellationFlag(const std::atomic_bool *flag) {
            cancellation_ = flag;
        }
        const ExecutionStats &stats() const {
            return stats_;
        }

      private:
        struct Frame {
            uint32_t return_ip, function;
            size_t operand_base, local_base;
        };
        const std::shared_ptr<const CompiledProgram> program_;
        const Bytecode &bc_;
        const VerificationResult &verified_;
        std::vector<long long> operands_, locals_;
        std::vector<Frame> frames_;
        uint32_t ip_{0}, current_pc_{0};
        ExecutionLimits limits_;
        ExecutionStats stats_;
        const std::atomic_bool *cancellation_{nullptr};
        JitMode jit_mode_{JitMode::Auto};
        uint32_t hot_threshold_{1};
        bool trace_enabled_{false};
        uint64_t trace_limit_{0};
#if defined(MPLX_WITH_JIT)
        std::vector<uint32_t> hot_counts_;
#endif
        long long execute(uint32_t function, std::span<const long long> arguments);
        long long interpret();
        void enter(uint32_t function, uint32_t return_ip);
        void push(long long value);
        long long pop();
        void reset();
        SourceSpan sourceSpan(uint32_t pc) const;
        [[noreturn]] void fail(RuntimeErrorCode code, const std::string &message) const;
    };
} // namespace mplx
