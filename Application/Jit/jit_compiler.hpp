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
  };

} // namespace mplx::jit
