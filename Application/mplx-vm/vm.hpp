#pragma once
#include "../mplx-compiler/bytecode.hpp"
#include <vector>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace mplx {

struct VMValue { long long i; };

struct CallFrame { uint32_t ip; uint32_t fn; uint32_t bp; uint8_t arity; uint16_t locals; };

class VM {
public:
  explicit VM(const Bytecode& bc) : bc_(bc) {}
  long long run(const std::string& entry = "main");

private:
  const Bytecode& bc_;
  std::vector<VMValue> stack_;
  std::vector<CallFrame> frames_;
  uint32_t ip_{0};

#if defined(MPLX_WITH_JIT)
  // JIT placeholders for future integration
  // e.g., pointers to compiled entries cache, runtime helpers
public:
  using JitEntryPtr = long long(*)(void*);
  std::unordered_map<uint32_t, JitEntryPtr> jitted_;
private:
#endif

  void push(long long x){ stack_.push_back(VMValue{x}); }
  long long pop(){ auto v=stack_.back().i; stack_.pop_back(); return v; }
};

} // namespace mplx