#include "jit_compiler.hpp"
#include "jit_runtime.hpp"
#include "platform.hpp"
#include "../mplx-compiler/bytecode.hpp"
#include <cstring>

namespace mplx::jit {

std::optional<JitCompiled> JitCompiler::compileFunction(const CompileCtx& ctx){
  // MVP: emit prologue; mov rax, 0; epilogue; ret
  X64Emitter e;
  e.prologue();
  e.mov_rax_imm(0);
  e.epilogue();
  e.ret();

  auto ex = plat::alloc_executable(e.buf.size());
  if (!ex.ptr) return std::nullopt;
  std::memcpy(ex.ptr, e.buf.data(), e.buf.size());
  plat::flush_icache(ex.ptr, e.buf.size());

  JitCompiled out;
  out.mem.reset(reinterpret_cast<uint8_t*>(ex.ptr));
  out.size = e.buf.size();
  out.entry = reinterpret_cast<JitEntryPtr>(ex.ptr);
  return out;
}

} // namespace mplx::jit


