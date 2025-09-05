#pragma once
#include "x64_emitter.hpp"
#include <cstdint>
#include <optional>

namespace mplx {
  struct Bytecode;
}

namespace mplx::jit {

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
