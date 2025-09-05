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
    // Labels and fixups
    struct Fixup { size_t pos; int label; enum Kind { JMP, JZ, JNZ } kind; };
    std::vector<size_t> label_pos;
    std::vector<Fixup> fixups;
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
      // and rsp, -16 (mask)
      and_rsp_imm8(0xF0);
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
    // Label API
    int create_label() {
      label_pos.push_back((size_t)-1);
      return (int)label_pos.size() - 1;
    }
    void bind_label(int id) {
      if (id >= 0 && (size_t)id < label_pos.size())
        label_pos[(size_t)id] = buf.size();
    }
    void finalize_fixups() {
      for (auto &f : fixups) {
        if ((size_t)f.label >= label_pos.size()) continue;
        size_t target = label_pos[(size_t)f.label];
        if (target == (size_t)-1) continue;
        int64_t rel = (int64_t)target - (int64_t)(f.pos + 4);
        for (int i = 0; i < 4; ++i) buf.bytes[f.pos + i] = uint8_t((rel >> (i * 8)) & 0xFF);
      }
    }
    // mov rax, imm64
    void mov_rax_imm(uint64_t imm) {
      buf.emit_u8(0x48);
      buf.emit_u8(0xB8);
      buf.emit_u64(imm);
    }
    // mov rbx, imm64
    void mov_rbx_imm(uint64_t imm) {
      buf.emit_u8(0x48);
      buf.emit_u8(0xBB);
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
    // imul rax, rbx
    void imul_rax_rbx() { buf.emit_u8(0x48); buf.emit_u8(0x0F); buf.emit_u8(0xAF); buf.emit_u8(0xC3); }
    // cqo
    void cqo() { buf.emit_u8(0x48); buf.emit_u8(0x99); }
    // idiv rbx
    void idiv_rbx() { buf.emit_u8(0x48); buf.emit_u8(0xF7); buf.emit_u8(0xFB); }
    // mov rax, rdx
    void mov_rax_rdx() { buf.emit_u8(0x48); buf.emit_u8(0x89); buf.emit_u8(0xD0); }
    // test rax, rax
    void test_rax_rax() { buf.emit_u8(0x48); buf.emit_u8(0x85); buf.emit_u8(0xC0); }
    // and rsp, imm8 (mask)
    void and_rsp_imm8(uint8_t imm) { buf.emit_u8(0x48); buf.emit_u8(0x83); buf.emit_u8(0xE4); buf.emit_u8(imm); }
    // jmp/jz/jnz to label (rel32)
    void jmp_label(int label) { buf.emit_u8(0xE9); size_t at = buf.size(); buf.emit_u32(0); fixups.push_back(Fixup{at, label, Fixup::JMP}); }
    void jz_label(int label) { buf.emit_u8(0x0F); buf.emit_u8(0x84); size_t at = buf.size(); buf.emit_u32(0); fixups.push_back(Fixup{at, label, Fixup::JZ}); }
    void jnz_label(int label) { buf.emit_u8(0x0F); buf.emit_u8(0x85); size_t at = buf.size(); buf.emit_u32(0); fixups.push_back(Fixup{at, label, Fixup::JNZ}); }
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
