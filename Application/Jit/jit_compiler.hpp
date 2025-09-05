#pragma once
#include "x64_emitter.hpp"
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>
#include <memory>

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

  struct CompileCtx {
    const Bytecode *bc{nullptr};
    uint32_t fnIndex{0};
  };


  // Minimal translator: supports sequence [OP_PUSH_CONST, OP_RET] and emits return-immediate.
  class JitCompiler {
  public:
    std::optional<JitCompiled> compileFunction(const CompileCtx &ctx);
    // diagnostics
    bool enable_dump{false}; // set by env or CLI
    // filled per compile
    std::vector<std::pair<uint32_t, size_t>> bc_to_mc; // bytecode ip -> machine-code offset
  };

} // namespace mplx::jit
