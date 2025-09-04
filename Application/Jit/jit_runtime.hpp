#pragma once
#include <cstdint>
#include <unordered_map>
#include <memory>
#include "x64_emitter.hpp"

namespace mplx { struct Bytecode; }

namespace mplx::jit {

using JitEntryPtr = long long(*)(void* vm_state);

struct JitCompiled {
  std::unique_ptr<uint8_t[]> mem;
  size_t size{0};
  JitEntryPtr entry{nullptr};
};

class JitRuntime {
public:
  JitRuntime() = default;
  // For MVP we just expose a stub that returns constant 0
  JitCompiled compileReturnZero();
};

} // namespace mplx::jit


