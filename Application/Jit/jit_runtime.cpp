#include "jit_runtime.hpp"
#include <windows.h>

namespace mplx::jit {

static void flush_icache(void* p, size_t n){
#if defined(_WIN32)
  FlushInstructionCache(GetCurrentProcess(), p, n);
#endif
}

JitCompiled JitRuntime::compileReturnZero(){
  X64Emitter e;
  e.mov_rax_imm(0);
  e.ret();

  size_t sz = e.buf.size();
  auto mem = (uint8_t*)::VirtualAlloc(nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!mem) return {};
  std::memcpy(mem, e.buf.data(), sz);
  flush_icache(mem, sz);

  JitCompiled out;
  out.mem.reset(mem);
  out.size = sz;
  out.entry = reinterpret_cast<JitEntryPtr>(mem);
  return out;
}

} // namespace mplx::jit


