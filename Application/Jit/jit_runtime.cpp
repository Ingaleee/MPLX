#include "jit_compiler.hpp"
#include <algorithm>
#include <array>

namespace mplx::jit {
    namespace {
// Clang's function sanitizer reads compiler-emitted type metadata before an
// indirect target. Generated machine code has no such metadata. Suppress only
// that check at the generated-code boundary; ASan and other UBSan checks remain.
#if defined(__clang__)
        __attribute__((no_sanitize("function"), noinline))
#endif
        long long
        invoke_generated(JitEntryPtr entry, ExecutionContext *context) {
            return entry(context);
        }
        struct NativeFrame {
            uint32_t function;
            size_t arena_base, return_slot;
            uint64_t prefix, previous_locals;
            void *continuation;
        };
        struct NativeState {
            const Bytecode &bytecode;
            const JitCompiled &module;
            std::vector<long long> arena;
            std::vector<NativeFrame> frames;
            std::span<const long long> arguments;
        };
        void error(ExecutionContext *context, NativeError value) {
            context->error = static_cast<uint32_t>(value);
        }
    } // namespace
    extern "C" void *native_enter(ExecutionContext *c, uint32_t function, uint32_t height,
                                  void *continuation) noexcept {
        auto &state = *static_cast<NativeState *>(c->state);
        try {
            if (function >= state.bytecode.functions.size()) {
                error(c, NativeError::Internal);
                return nullptr;
            }
            const auto &meta = state.bytecode.functions[function];
            if (state.frames.size() >= c->frame_limit) {
                error(c, NativeError::FrameLimit);
                return nullptr;
            }
            std::array<long long, 255> arguments{};
            size_t return_slot = 0;
            uint64_t prefix = 0;
            if (!state.frames.empty()) {
                const auto &caller = state.frames.back();
                const auto &caller_meta = state.bytecode.functions[caller.function];
                if (height < meta.arity) {
                    error(c, NativeError::Internal);
                    return nullptr;
                }
                return_slot = caller.arena_base + caller_meta.locals + height - meta.arity;
                prefix = caller.prefix + height - meta.arity;
                for (uint32_t a = 0; a < meta.arity; ++a)
                    arguments[a] = state.arena[return_slot + a];
            } else {
                if (state.arguments.size() != meta.arity) {
                    error(c, NativeError::Internal);
                    return nullptr;
                }
                std::copy(state.arguments.begin(), state.arguments.end(), arguments.begin());
            }
            if (prefix > c->value_limit || c->local_values > c->value_limit - prefix ||
                meta.locals > c->value_limit - prefix - c->local_values) {
                error(c, NativeError::ValueLimit);
                return nullptr;
            }
            const auto base = state.arena.size();
            const auto slots = static_cast<size_t>(meta.locals) + state.module.metadata[function].max_stack;
            state.arena.resize(base + slots, 0);
            for (uint32_t a = 0; a < meta.arity; ++a)
                state.arena[base + a] = arguments[a];
            state.frames.push_back({function, base, return_slot, prefix, c->local_values, continuation});
            c->local_values += meta.locals;
            c->operand_prefix = prefix;
            c->active_base = state.arena.data() + base;
            c->depth = state.frames.size();
            c->peak_frames = std::max(c->peak_frames, c->depth);
            c->peak_locals = std::max(c->peak_locals, c->local_values);
            if (continuation)
                ++c->native_calls;
            return state.module.functions[function];
        } catch (...) {
            error(c, NativeError::Internal);
            return nullptr;
        }
    }
    extern "C" void *native_return(ExecutionContext *c, long long value) noexcept {
        auto &state = *static_cast<NativeState *>(c->state);
        if (state.frames.empty()) {
            error(c, NativeError::Internal);
            return nullptr;
        }
        const auto frame = state.frames.back();
        state.frames.pop_back();
        state.arena.resize(frame.arena_base);
        c->local_values = frame.previous_locals;
        c->depth = state.frames.size();
        if (state.frames.empty()) {
            c->result = value;
            c->active_base = nullptr;
            c->operand_prefix = 0;
            return nullptr;
        }
        const auto &caller = state.frames.back();
        state.arena[frame.return_slot] = value;
        c->active_base = state.arena.data() + caller.arena_base;
        c->operand_prefix = caller.prefix;
        c->function = caller.function;
        return frame.continuation;
    }
    NativeResult execute_native(const Bytecode &bytecode, const JitCompiled &module, uint32_t entry,
                                uint64_t instructions, uint64_t frames, uint64_t values,
                                std::span<const long long> arguments) {
        NativeState state{bytecode, module, {}, {}, arguments};
        ExecutionContext c;
        c.remaining = instructions;
        c.state = &state;
        c.frame_limit = frames;
        c.value_limit = values;
        c.function = entry;
        c.pc = 0;
        c.peak_operands = arguments.size();
        c.entry = native_enter(&c, entry, 0, nullptr);
        if (c.entry)
            (void)invoke_generated(module.entry, &c);
        return {c.result,
                static_cast<NativeError>(c.error),
                c.pc,
                c.function,
                instructions - c.remaining,
                c.peak_operands,
                c.peak_locals,
                c.peak_frames,
                c.native_calls};
    }
} // namespace mplx::jit
