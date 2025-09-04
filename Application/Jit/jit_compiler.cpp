#include "jit_compiler.hpp"
#include "jit_runtime.hpp"
#include "platform.hpp"
#include "../mplx-compiler/bytecode.hpp"
#include <cstring>

namespace mplx::jit {

static uint32_t read_u32(const std::vector<uint8_t>& code, uint32_t& ip){
  uint32_t v = (uint32_t)code[ip] | ((uint32_t)code[ip+1]<<8) | ((uint32_t)code[ip+2]<<16) | ((uint32_t)code[ip+3]<<24);
  ip += 4; return v;
}

std::optional<JitCompiled> JitCompiler::compileFunction(const CompileCtx& ctx){
  // v0: если функция состоит только из PUSH_CONST и бинарных {ADD,SUB,MUL,DIV} и заканчивается RET,
  // посчитаем значение на этапе JIT и вернём mov rax, <const>.
  const Bytecode& bc = *ctx.bc;
  if (ctx.fnIndex >= bc.functions.size()) return std::nullopt;
  const auto& fn = bc.functions[ctx.fnIndex];
  uint32_t ip = fn.entry;
  std::vector<long long> st;
  bool ok = true;
  while (ip < bc.code.size()){
    Op op = (Op)bc.code[ip++];
    if (op == OP_PUSH_CONST){ uint32_t ci = read_u32(bc.code, ip); if (ci >= bc.consts.size()){ ok=false; break; } st.push_back(bc.consts[ci]); }
    else if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV){ if (st.size()<2){ ok=false; break; } long long b=st.back(); st.pop_back(); long long a=st.back(); st.pop_back(); long long r=0; if(op==OP_ADD) r=a+b; else if(op==OP_SUB) r=a-b; else if(op==OP_MUL) r=a*b; else { if (b==0){ ok=false; break; } r=a/b; } st.push_back(r); }
    else if (op == OP_RET){ break; }
    else { ok=false; break; }
  }
  if (!ok || st.empty()) return std::nullopt;
  long long result = st.back();

  X64Emitter e;
  e.prologue();
  e.mov_rax_imm((uint64_t)result);
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


