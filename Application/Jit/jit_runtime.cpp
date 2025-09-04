#include "jit_runtime.hpp"
#include "platform.hpp"
#include <cstring>

namespace mplx::jit {

JitCompiled JitRuntime::compileReturnZero(){
  X64Emitter e;
  e.mov_rax_imm(0);
  e.ret();

  size_t sz = e.buf.size();
  auto ex = plat::alloc_executable(sz);
  if (!ex.ptr) return {};
  std::memcpy(ex.ptr, e.buf.data(), sz);
  plat::flush_icache(ex.ptr, sz);

  JitCompiled out;
  out.mem.reset(reinterpret_cast<uint8_t*>(ex.ptr));
  out.size = sz;
  out.entry = reinterpret_cast<JitEntryPtr>(ex.ptr);
  return out;
}

} // namespace mplx::jit


