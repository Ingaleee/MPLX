#include "jit_runtime.hpp"
#include "platform.hpp"
#include <cstring>

namespace mplx::jit {

  JitCompiled JitRuntime::compileReturnZero() {
    X64Emitter e;
    e.mov_rax_imm(0);
    e.ret();

    size_t sz = e.buf.size();
    auto ex   = plat::alloc_executable(sz);
    if (!ex.ptr)
      return {};
    std::memcpy(ex.ptr, e.buf.data(), sz);
    plat::flush_icache(ex.ptr, sz);

    JitCompiled out;
    out.mem.reset(reinterpret_cast<uint8_t *>(ex.ptr));
    out.size  = sz;
    out.entry = reinterpret_cast<JitEntryPtr>(ex.ptr);
    return out;
  }

  JitCompiled JitRuntime::compileRuntimeCall(RuntimeCallFn fn, uint32_t fnIndex) {
    // For v0 we do a very small stub that moves args into registers and calls the helper.
    // Signature: long long helper(void* vm, uint32_t fnIndex)
    // Windows x64: rcx, rdx, r8, r9
    // System V: rdi, rsi, rdx, rcx
    X64Emitter e;
    e.prologue();
#if MPLX_WIN
    // rcx = vm (1st arg), rdx = fnIndex (2nd arg)
    // mov rdx, imm32 (zero-extend)
    e.buf.emit_u8(0x48); e.buf.emit_u8(0xC7); e.buf.emit_u8(0xC2); // mov rdx, imm32
    e.buf.emit_u32(fnIndex);
    // mov rax, imm64; mov r8, rax (load helper address to rax, then use call rax)
    e.mov_rax_imm((uint64_t)fn);
    // call rax
    e.buf.emit_u8(0xFF); e.buf.emit_u8(0xD0);
#else
    // System V: rdi=vm, rsi=fnIndex
    // mov rsi, imm32
    e.buf.emit_u8(0x48); e.buf.emit_u8(0xC7); e.buf.emit_u8(0xC6); // mov rsi, imm32
    e.buf.emit_u32(fnIndex);
    e.mov_rax_imm((uint64_t)fn);
    e.buf.emit_u8(0xFF); e.buf.emit_u8(0xD0);
#endif
    e.epilogue();
    e.ret();

    size_t sz = e.buf.size();
    auto ex   = plat::alloc_executable(sz);
    if (!ex.ptr) return {};
    std::memcpy(ex.ptr, e.buf.data(), sz);
    plat::flush_icache(ex.ptr, sz);

    JitCompiled out;
    out.mem.reset(reinterpret_cast<uint8_t*>(ex.ptr));
    out.size  = sz;
    out.entry = reinterpret_cast<JitEntryPtr>(ex.ptr);
    return out;
  }

} // namespace mplx::jit
