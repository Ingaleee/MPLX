#pragma once
#include "../mplx-compiler/bytecode.hpp"
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace mplx {

  struct VMValue {
    long long i;
  };

  struct CallFrame {
    uint32_t ip;
    uint32_t fn;
    uint32_t bp;
    uint8_t arity;
    uint16_t locals;
  };

  class VM {
  public:
    explicit VM(const Bytecode &bc) : bc_(bc) {}
    long long run(const std::string &entry = "main");
    // v0 JIT helper: run by function index (no argument marshalling beyond VM's own stack)
    long long runByIndex(uint32_t fnIndex);

    // Minimal ABI snapshot for JIT codegen (stable layout)
    struct JitVmState {
      long long *stack_ptr{nullptr};
      uint64_t sp_index{0};
      uint64_t bp_index{0};
    };
    const JitVmState &jitState() const { return jit_state_; }

  private:
    const Bytecode &bc_;
    std::vector<VMValue> stack_;
    std::vector<CallFrame> frames_;
    uint32_t ip_{0};
    JitVmState jit_state_{};

#if defined(MPLX_WITH_JIT)
    // JIT placeholders for future integration
    // e.g., pointers to compiled entries cache, runtime helpers
  public:
    using JitEntryPtr = long long (*)(void *);
    std::unordered_map<uint32_t, JitEntryPtr> jitted_;
    std::unordered_map<uint32_t, std::unique_ptr<uint8_t[]>> jit_mem_;

  private:
#endif

    void push(long long x) {
      stack_.push_back(VMValue{x});
      // keep ABI state in sync
      jit_state_.stack_ptr = (stack_.empty() ? nullptr : &stack_[0].i);
      jit_state_.sp_index  = (uint64_t)stack_.size();
    }
    long long pop() {
      auto v = stack_.back().i;
      stack_.pop_back();
      jit_state_.stack_ptr = (stack_.empty() ? nullptr : &stack_[0].i);
      jit_state_.sp_index  = (uint64_t)stack_.size();
      return v;
    }
  };

} // namespace mplx