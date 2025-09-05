#pragma once
#include "x64_emitter.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace mplx {
  struct Bytecode;
}

namespace mplx::jit {

  using JitEntryPtr = long long (*)(void *vm_state);

  struct JitCompiled {
    std::unique_ptr<uint8_t[]> mem;
    size_t size{0};
    JitEntryPtr entry{nullptr};
  };

  class JitRuntime {
  public:
    JitRuntime() = default;
    // For MVP: stubs
    JitCompiled compileReturnZero();
    // v0: runtime helper call trampoline (placeholder for CALL n)
    using RuntimeCallFn = long long (*)(void *vm_state, uint32_t fnIndex);
    JitCompiled compileRuntimeCall(RuntimeCallFn fn, uint32_t fnIndex);
  };

  extern "C" long long vm_runtime_call(void *vm_state, uint32_t fnIndex);

  // Runtime stubs for JIT (v0)
  extern "C" long long jit_runtime_call(void *vm_state, uint32_t fnIndex, uint32_t argc);
  extern "C" void jit_runtime_trap_div0(void *vm_state);
  extern "C" long long jit_runtime_stub(void *vm_state, uint32_t opcode, uint64_t a0, uint64_t a1);

} // namespace mplx::jit
