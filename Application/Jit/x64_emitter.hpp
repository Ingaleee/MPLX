#pragma once
#include <cstdint>
#include <vector>

namespace mplx::jit {

struct CodeBuffer {
  std::vector<uint8_t> bytes;
  void emit_u8(uint8_t v){ bytes.push_back(v); }
  void emit_u32(uint32_t v){ for(int i=0;i<4;++i) emit_u8(uint8_t((v>>(i*8))&0xFF)); }
  void emit_u64(uint64_t v){ for(int i=0;i<8;++i) emit_u8(uint8_t((v>>(i*8))&0xFF)); }
  uint8_t* data(){ return bytes.data(); }
  size_t size() const { return bytes.size(); }
};

// Minimal x86-64 emitter for mov/add/sub/cmp/jcc/ret (MVP)
struct X64Emitter {
  CodeBuffer buf;
  // mov rax, imm64
  void mov_rax_imm(uint64_t imm){ buf.emit_u8(0x48); buf.emit_u8(0xB8); buf.emit_u64(imm); }
  // add rax, rbx
  void add_rax_rbx(){ buf.emit_u8(0x48); buf.emit_u8(0x01); buf.emit_u8(0xD8); }
  // sub rax, rbx
  void sub_rax_rbx(){ buf.emit_u8(0x48); buf.emit_u8(0x29); buf.emit_u8(0xD8); }
  // ret
  void ret(){ buf.emit_u8(0xC3); }
};

} // namespace mplx::jit


