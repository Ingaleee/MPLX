#pragma once
#include <cstdint>
#include <vector>

namespace mplx::jit {

  struct CodeBuffer {
    std::vector<uint8_t> bytes;
    void emit_u8(uint8_t v) {
      bytes.push_back(v);
    }
    void emit_u32(uint32_t v) {
      for (int i = 0; i < 4; ++i)
        emit_u8(uint8_t((v >> (i * 8)) & 0xFF));
    }
    void emit_u64(uint64_t v) {
      for (int i = 0; i < 8; ++i)
        emit_u8(uint8_t((v >> (i * 8)) & 0xFF));
    }
    uint8_t *data() {
      return bytes.data();
    }
    size_t size() const {
      return bytes.size();
    }
  };

  // Minimal x86-64 emitter for mov/add/sub/cmp/jcc/ret (MVP)
  // Minimal Windows x64 / System V compatible subset (we avoid args for v0)
  struct X64Emitter {
    CodeBuffer buf;
    // Prologue/Epilogue with ABI bits (shadow space on Win, callee-saved regs)
    void prologue() {
      // push rbp; mov rbp,rsp
      buf.emit_u8(0x55);
      buf.emit_u8(0x48);
      buf.emit_u8(0x89);
      buf.emit_u8(0xE5);
#if MPLX_WIN
      // sub rsp, 32  (shadow space)
      buf.emit_u8(0x48);
      buf.emit_u8(0x83);
      buf.emit_u8(0xEC);
      buf.emit_u8(0x20);
#endif
      // save callee-saved rbx, r12..r15
      push_rbx();
      buf.emit_u8(0x41);
      buf.emit_u8(0x54); // push r12
      buf.emit_u8(0x41);
      buf.emit_u8(0x55); // push r13
      buf.emit_u8(0x41);
      buf.emit_u8(0x56); // push r14
      buf.emit_u8(0x41);
      buf.emit_u8(0x57); // push r15
    }
    void epilogue() {
      // restore r15..r12
      buf.emit_u8(0x41);
      buf.emit_u8(0x5F); // pop r15
      buf.emit_u8(0x41);
      buf.emit_u8(0x5E); // pop r14
      buf.emit_u8(0x41);
      buf.emit_u8(0x5D); // pop r13
      buf.emit_u8(0x41);
      buf.emit_u8(0x5C); // pop r12
      // restore rbx
      pop_rbx();
#if MPLX_WIN
      // add rsp, 32 (shadow space)
      buf.emit_u8(0x48);
      buf.emit_u8(0x83);
      buf.emit_u8(0xC4);
      buf.emit_u8(0x20);
#endif
      // mov rsp, rbp; pop rbp
      buf.emit_u8(0x48);
      buf.emit_u8(0x89);
      buf.emit_u8(0xEC);
      buf.emit_u8(0x5D);
    }
    // mov rax, imm64
    void mov_rax_imm(uint64_t imm) {
      buf.emit_u8(0x48);
      buf.emit_u8(0xB8);
      buf.emit_u64(imm);
    }
    // add rax, rbx
    void add_rax_rbx() {
      buf.emit_u8(0x48);
      buf.emit_u8(0x01);
      buf.emit_u8(0xD8);
    }
    // sub rax, rbx
    void sub_rax_rbx() {
      buf.emit_u8(0x48);
      buf.emit_u8(0x29);
      buf.emit_u8(0xD8);
    }
    // push rbx (callee-saved)
    void push_rbx() {
      buf.emit_u8(0x53);
    }
    // pop rbx
    void pop_rbx() {
      buf.emit_u8(0x5B);
    }
    // ret
    void ret() {
      buf.emit_u8(0xC3);
    }
  };

} // namespace mplx::jit
