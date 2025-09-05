#include "jit_compiler.hpp"
#include "../mplx-compiler/bytecode.hpp"
#include "jit_runtime.hpp"
#include "platform.hpp"
#include "../mplx-vm/vm.hpp"
#include <cstring>

namespace mplx::jit {

  static uint32_t read_u32(const std::vector<uint8_t> &code, uint32_t &ip) {
    uint32_t v = (uint32_t)code[ip] | ((uint32_t)code[ip + 1] << 8) | ((uint32_t)code[ip + 2] << 16) | ((uint32_t)code[ip + 3] << 24);
    ip += 4;
    return v;
  }

  std::optional<JitCompiled> JitCompiler::compileFunction(const CompileCtx &ctx) {
    // detect dump mode via env once per call (cheap)
    if (const char *env = std::getenv("MPLX_JIT_DUMP")) {
      enable_dump = (std::strcmp(env, "0") != 0);
    }
    bc_to_mc.clear();
    // v0: если функция состоит из поддерживаемых опкодов (PUSH_CONST, арифметика, сравнения,
    // JMP/JMP_IF_FALSE с вычислимым условием, RET), — вычисляем результат при JIT и эмитим mov rax,imm.
    const Bytecode &bc = *ctx.bc;
    if (ctx.fnIndex >= bc.functions.size())
      return std::nullopt;
    const auto &fn = bc.functions[ctx.fnIndex];
    uint32_t ip    = fn.entry;
    std::vector<long long> st;
    std::vector<long long> locals;
    locals.resize(fn.locals, 0);
    bool ok   = true;
    int guard = 0;
    while (ip < bc.code.size() && guard++ < 100000) {
      Op op = (Op)bc.code[ip++];

      if (op == OP_PUSH_CONST) {
        uint32_t ci = read_u32(bc.code, ip);
        if (ci >= bc.consts.size()) {
          ok = false;
          break;
        }
        st.push_back(bc.consts[ci]);
        continue;
      }

      if (op == OP_LOAD_LOCAL) {
        uint32_t idx = read_u32(bc.code, ip);
        if (idx >= locals.size()) {
          ok = false;
          break;
        }
        st.push_back(locals[(size_t)idx]);
        continue;
      }

      if (op == OP_LOAD_LOCAL8) {
        if (ip >= bc.code.size()) { ok = false; break; }
        uint32_t idx = bc.code[ip++];
        if (idx >= locals.size()) { ok = false; break; }
        st.push_back(locals[(size_t)idx]);
        continue;
      }

      if (op == OP_LD0 || op == OP_LD1 || op == OP_LD2 || op == OP_LD3) {
        uint32_t idx = (op == OP_LD0 ? 0u : op == OP_LD1 ? 1u : op == OP_LD2 ? 2u : 3u);
        if (idx >= locals.size()) { ok = false; break; }
        st.push_back(locals[(size_t)idx]);
        continue;
      }

      if (op == OP_STORE_LOCAL) {
        uint32_t idx = read_u32(bc.code, ip);
        if (st.empty() || idx >= locals.size()) {
          ok = false;
          break;
        }
        long long v = st.back();
        st.pop_back();
        locals[(size_t)idx] = v;
        st.push_back(v);
        continue;
      }

      if (op == OP_STORE_LOCAL8) {
        if (ip >= bc.code.size()) { ok = false; break; }
        uint32_t idx = bc.code[ip++];
        if (st.empty() || idx >= locals.size()) { ok = false; break; }
        long long v = st.back(); st.pop_back();
        locals[(size_t)idx] = v; st.push_back(v);
        continue;
      }

      if (op == OP_ST0 || op == OP_ST1 || op == OP_ST2 || op == OP_ST3) {
        uint32_t idx = (op == OP_ST0 ? 0u : op == OP_ST1 ? 1u : op == OP_ST2 ? 2u : 3u);
        if (st.empty() || idx >= locals.size()) { ok = false; break; }
        long long v = st.back(); st.pop_back();
        locals[(size_t)idx] = v; st.push_back(v);
        continue;
      }

      if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD) {
        if (st.size() < 2) {
          ok = false;
          break;
        }
        long long b = st.back();
        st.pop_back();
        long long a = st.back();
        st.pop_back();
        long long r = 0;
        if (op == OP_ADD)
          r = a + b;
        else if (op == OP_SUB)
          r = a - b;
        else if (op == OP_MUL)
          r = a * b;
        else if (op == OP_DIV) {
          if (b == 0) {
            ok = false;
            break;
          }
          r = a / b;
        } else { // OP_MOD
          if (b == 0) {
            ok = false;
            break;
          }
          r = a % b;
        }
        st.push_back(r);
        continue;
      }

      if (op == OP_NEG) {
        if (st.empty()) {
          ok = false;
          break;
        }
        long long a = st.back();
        st.back()   = -a;
        continue;
      }

      if (op == OP_POP) {
        if (st.empty()) {
          ok = false;
          break;
        }
        st.pop_back();
        continue;
      }

      if (op == OP_EQ || op == OP_NE || op == OP_LT || op == OP_LE || op == OP_GT || op == OP_GE) {
        if (st.size() < 2) {
          ok = false;
          break;
        }
        long long b = st.back();
        st.pop_back();
        long long a = st.back();
        st.pop_back();
        long long r = 0;
        if (op == OP_EQ)
          r = (a == b);
        else if (op == OP_NE)
          r = (a != b);
        else if (op == OP_LT)
          r = (a < b);
        else if (op == OP_LE)
          r = (a <= b);
        else if (op == OP_GT)
          r = (a > b);
        else
          r = (a >= b);
        st.push_back(r);
        continue;
      }

      if (op == OP_JMP) {
        uint32_t dst = read_u32(bc.code, ip);
        ip           = dst;
        continue;
      }

      if (op == OP_JMP_IF_FALSE) {
        uint32_t dst = read_u32(bc.code, ip);
        if (st.empty()) {
          ok = false;
          break;
        }
        long long c = st.back();
        st.pop_back();
        if (!c)
          ip = dst;
        continue;
      }

      if (op == OP_JMP_IF_TRUE) {
        uint32_t dst = read_u32(bc.code, ip);
        if (st.empty()) {
          ok = false;
          break;
        }
        long long c = st.back();
        st.pop_back();
        if (c)
          ip = dst;
        continue;
      }

      if (op == OP_AND) {
        if (st.size() < 2) { ok = false; break; }
        long long b = st.back(); st.pop_back();
        long long a = st.back(); st.pop_back();
        st.push_back((a != 0) && (b != 0));
        continue;
      }

      if (op == OP_OR) {
        if (st.size() < 2) { ok = false; break; }
        long long b = st.back(); st.pop_back();
        long long a = st.back(); st.pop_back();
        st.push_back((a != 0) || (b != 0));
        continue;
      }

      if (op == OP_NOT) {
        if (st.empty()) { ok = false; break; }
        long long a = st.back(); st.back() = (a == 0);
        continue;
      }

      if (op == OP_RET) {
        break;
      }

      if (op == OP_CALL) {
        // v0: we do not inline calls; stop JIT for this function for now
        ok = false;
        break;
      }

      ok = false;
      break;
    }
    if (!ok)
      return std::nullopt;

    // If we reach here, we successfully compiled the entire function
    X64Emitter e;
    std::vector<std::pair<uint32_t, size_t>> bc_to_mc;
    e.emit_canaries = true;
    e.prologue();
    // Map VM::jitState fields into registers from rcx (Win) / rdi (SysV). We assume Win here for brevity.
    // rcx -> VM*, [rcx + offset(stack_ptr)] -> r13, [rcx + offset(sp_index)] -> r12, [rcx + offset(bp_index)] -> rbx
    const uint32_t off_stack_ptr = 0; // placeholder
    const uint32_t off_sp_index  = 8; // placeholder  
    const uint32_t off_bp_index  = 16; // placeholder
    e.mov_r13_m_rcx_disp32(off_stack_ptr);
    e.mov_r12_m_rcx_disp32(off_sp_index);
    e.mov_rbx_m_rcx_disp32(off_bp_index);

    // Two-pass prep: compute label ids for jump targets in this function
    std::unordered_map<uint32_t, int> ip_to_label;
    {
      // leaders: function entry and all jump targets
      auto record_label = [&](uint32_t target_ip) {
        if (ip_to_label.find(target_ip) == ip_to_label.end()) {
          int lid                 = e.create_label();
          ip_to_label[target_ip] = lid;
        }
      };
      record_label(fn.entry);
      uint32_t sip = fn.entry;
      while (sip < bc.code.size()) {
        Op sop = (Op)bc.code[sip++];
        if (sop == OP_JMP) {
          uint32_t dst = read_u32(bc.code, sip);
          record_label(dst);
        } else if (sop == OP_JMP_IF_FALSE || sop == OP_JMP_IF_TRUE) {
          uint32_t dst = read_u32(bc.code, sip);
          record_label(dst);
        } else if (sop == OP_CALL || sop == OP_PUSH_CONST || sop == OP_LOAD_LOCAL || sop == OP_STORE_LOCAL) {
          (void)read_u32(bc.code, sip);
        } else if (sop == OP_LOAD_LOCAL8 || sop == OP_STORE_LOCAL8) {
          if (sip < bc.code.size()) ++sip;
        }
        if (sop == OP_RET || sop == OP_HALT) break;
      }
    }

    // Second pass: bind labels as we reach their ips, and emit branches to labels
    {
      uint32_t gip = fn.entry;
      while (gip < bc.code.size()) {
        if (auto itl = ip_to_label.find(gip); itl != ip_to_label.end()) {
          e.bind_label(itl->second);
        }
        Op gop = (Op)bc.code[gip++];
        // TODO: map VM::jitState to registers (r13=stack base, r12=sp index, rbx=bp index)
        // For now, we only handle control flow here; data ops fallback to MVP path below.
        if (gop == OP_JMP) {
          uint32_t dst = read_u32(bc.code, gip);
          int lid      = ip_to_label[dst];
          bc_to_mc.push_back({gip - 5, e.buf.size()});
          e.jmp_label(lid);
          continue;
        }
        if (gop == OP_JMP_IF_FALSE) {
          uint32_t dst = read_u32(bc.code, gip);
          int lid      = ip_to_label[dst];
          // Convention for now: test rax,rax (result of last calc) then jz
          bc_to_mc.push_back({gip - 5, e.buf.size()});
          e.test_rax_rax();
          e.jz_label(lid);
          continue;
        }
        if (gop == OP_JMP_IF_TRUE) {
          uint32_t dst = read_u32(bc.code, gip);
          int lid      = ip_to_label[dst];
          bc_to_mc.push_back({gip - 5, e.buf.size()});
          e.test_rax_rax();
          e.jnz_label(lid);
          continue;
        }
        if (gop == OP_PUSH_CONST) {
          uint32_t ci = read_u32(bc.code, gip);
          uint64_t imm = (ci < bc.consts.size()) ? (uint64_t)bc.consts[ci] : 0ull;
          bc_to_mc.push_back({gip - 5, e.buf.size()});
          e.mov_rax_imm(imm);
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
          continue;
        }
        if (gop == OP_LOAD_LOCAL) {
          uint32_t localIdx = read_u32(bc.code, gip);
          bc_to_mc.push_back({gip - 5, e.buf.size()});
          // Load local variable from [rbp + localIdx*8]
          e.mov_rax_m_rbx_disp32(localIdx * 8);
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
          continue;
        }
        if (gop == OP_STORE_LOCAL) {
          uint32_t localIdx = read_u32(bc.code, gip);
          bc_to_mc.push_back({gip - 5, e.buf.size()});
          // Store TOS to local variable [rbp + localIdx*8]
          e.dec_r12();
          e.mov_rax_m_r13_r12_s8_disp32(0);
          e.mov_m_rbx_disp32_rax(localIdx * 8);
          continue;
        }
        if (gop == OP_POP) {
          // pop TOS: dec sp
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          e.dec_r12();
          continue;
        }
        if (gop == OP_NEG) {
          // a = pop(); push(-a)
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          e.dec_r12();
          e.mov_rax_m_r13_r12_s8_disp32(0);
          // rax = -rax -> two's complement: neg rax (opcode F7 D8)
          e.buf.emit_u8(0x48); e.buf.emit_u8(0xF7); e.buf.emit_u8(0xD8);
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
          continue;
        }
        auto emit_binop = [&](uint8_t which){
          // pop b, pop a, rax = a op b, push rax
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          e.dec_r12();
          e.mov_rbx_m_r13_r12_s8_disp32(0);
          e.dec_r12();
          e.mov_rax_m_r13_r12_s8_disp32(0);
          if (which == 0) { // add
            e.add_rax_rbx();
          } else if (which == 1) { // sub
            e.sub_rax_rbx();
          } else if (which == 2) { // mul
            e.imul_rax_rbx();
          } else if (which == 3) { // div
            e.cqo();
            e.idiv_rbx();
          } else if (which == 4) { // mod -> rdx
            e.cqo();
            e.idiv_rbx();
            e.mov_rax_rdx();
          }
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
        };
        if (gop == OP_ADD) { emit_binop(0); continue; }
        if (gop == OP_SUB) { emit_binop(1); continue; }
        if (gop == OP_MUL) { emit_binop(2); continue; }
        if (gop == OP_DIV) { emit_binop(3); continue; }
        if (gop == OP_MOD) { emit_binop(4); continue; }

        auto emit_cmp = [&](uint8_t which){
          // pop b, pop a, rax = (a ? b) -> setcc to al, zero-extend
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          e.dec_r12(); e.mov_rbx_m_r13_r12_s8_disp32(0);
          e.dec_r12(); e.mov_rax_m_r13_r12_s8_disp32(0);
          // cmp rax, rbx
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x39); e.buf.emit_u8(0xD8);
          // setcc al
          uint8_t cc = 0x94; // setz as base
          switch (which) {
            case 0: cc = 0x94; break; // EQ: sete
            case 1: cc = 0x95; break; // NE: setne
            case 2: cc = 0x9C; break; // LT: setl
            case 3: cc = 0x9E; break; // LE: setle
            case 4: cc = 0x9F; break; // GT: setg
            case 5: cc = 0x9D; break; // GE: setge
          }
          e.buf.emit_u8(0x0F); e.buf.emit_u8(cc); e.buf.emit_u8(0xC0); // setcc al
          // movzx rax, al
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x0F); e.buf.emit_u8(0xB6); e.buf.emit_u8(0xC0);
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
        };
        if (gop == OP_EQ) { emit_cmp(0); continue; }
        if (gop == OP_NE) { emit_cmp(1); continue; }
        if (gop == OP_LT) { emit_cmp(2); continue; }
        if (gop == OP_LE) { emit_cmp(3); continue; }
        if (gop == OP_GT) { emit_cmp(4); continue; }
        if (gop == OP_GE) { emit_cmp(5); continue; }

        if (gop == OP_AND) {
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          // pop b, pop a, push((a!=0)&&(b!=0))
          e.dec_r12(); e.mov_rbx_m_r13_r12_s8_disp32(0);
          e.dec_r12(); e.mov_rax_m_r13_r12_s8_disp32(0);
          e.test_rax_rax(); e.buf.emit_u8(0x0F); e.buf.emit_u8(0x95); e.buf.emit_u8(0xC0); // setne al
          // store temp a!=0 in al->rax 0/1
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x0F); e.buf.emit_u8(0xB6); e.buf.emit_u8(0xC0);
          // test rbx,rbx ; setne bl ; and al, bl
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x85); e.buf.emit_u8(0xDB);
          e.buf.emit_u8(0x0F); e.buf.emit_u8(0x95); e.buf.emit_u8(0xC3); // setne bl
          e.buf.emit_u8(0x20); e.buf.emit_u8(0xC3); // and bl, al (wrong order), fix: and al, bl
          // Correct and al, bl
          e.buf.emit_u8(0x20); e.buf.emit_u8(0xD8); // and al, bl
          // movzx rax, al
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x0F); e.buf.emit_u8(0xB6); e.buf.emit_u8(0xC0);
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
          continue;
        }
        if (gop == OP_OR) {
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          e.dec_r12(); e.mov_rbx_m_r13_r12_s8_disp32(0);
          e.dec_r12(); e.mov_rax_m_r13_r12_s8_disp32(0);
          e.test_rax_rax(); e.buf.emit_u8(0x0F); e.buf.emit_u8(0x95); e.buf.emit_u8(0xC0); // setne al
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x0F); e.buf.emit_u8(0xB6); e.buf.emit_u8(0xC0);
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x85); e.buf.emit_u8(0xDB); // test rbx,rbx
          e.buf.emit_u8(0x0F); e.buf.emit_u8(0x95); e.buf.emit_u8(0xC3); // setne bl
          // or al, bl
          e.buf.emit_u8(0x08); e.buf.emit_u8(0xD8);
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x0F); e.buf.emit_u8(0xB6); e.buf.emit_u8(0xC0);
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
          continue;
        }
        if (gop == OP_NOT) {
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          e.dec_r12(); e.mov_rax_m_r13_r12_s8_disp32(0);
          e.test_rax_rax();
          // setz al ; movzx rax, al
          e.buf.emit_u8(0x0F); e.buf.emit_u8(0x94); e.buf.emit_u8(0xC0);
          e.buf.emit_u8(0x48); e.buf.emit_u8(0x0F); e.buf.emit_u8(0xB6); e.buf.emit_u8(0xC0);
          e.mov_m_r13_r12_s8_disp32_rax(0); e.inc_r12();
          continue;
        }
        if (gop == OP_CALL) {
          uint32_t fnIdx = read_u32(bc.code, gip);
          bc_to_mc.push_back({gip - 5, e.buf.size()});
#if MPLX_WIN
          // Windows x64: rcx=vm, rdx=fnIndex
          e.buf.emit_u8(0x48); e.buf.emit_u8(0xC7); e.buf.emit_u8(0xC2); // mov rdx, imm32
          e.buf.emit_u32(fnIdx);
#else
          // System V (future): rdi=vm (already), rsi=fnIndex
          e.buf.emit_u8(0x48); e.buf.emit_u8(0xC7); e.buf.emit_u8(0xC6); // mov rsi, imm32
          e.buf.emit_u32(fnIdx);
#endif
          // call vm_runtime_call
          e.mov_rax_imm((uint64_t)&vm_runtime_call);
          e.buf.emit_u8(0xFF); e.buf.emit_u8(0xD0); // call rax
          // push return value onto VM stack
          e.mov_m_r13_r12_s8_disp32_rax(0);
          e.inc_r12();
          continue;
        }
        if (gop == OP_RET) {
          bc_to_mc.push_back({gip - 1, e.buf.size()});
          // rax = pop(); persist sp_index back to VM; epilogue+ret
          e.dec_r12(); e.mov_rax_m_r13_r12_s8_disp32(0);
          e.mov_m_rcx_disp32_r12(off_sp_index);
          break;
        }
        // For v0 here, full opcode translation will be added. For now, fallthrough.
        if (gop == OP_RET) break;
        if (gop == OP_HALT) break;
        // Skip immediates to keep stream aligned
        if (gop == OP_PUSH_CONST || gop == OP_LOAD_LOCAL || gop == OP_STORE_LOCAL || gop == OP_CALL) {
          (void)read_u32(bc.code, gip);
        } else if (gop == OP_LOAD_LOCAL8 || gop == OP_STORE_LOCAL8) {
          if (gip < bc.code.size()) ++gip;
        }
      }
    }

    // Finalize fixups; if failed, gracefully fallback by returning nullopt
    e.finalize_fixups();
    if (!e.fixups_ok) {
      return std::nullopt;
    }

    // Now generate the actual JIT code
    auto ex = plat::alloc_executable(e.buf.size());
    if (!ex.ptr)
      return std::nullopt;
    std::memcpy(ex.ptr, e.buf.data(), e.buf.size());
    plat::flush_icache(ex.ptr, e.buf.size());

  if (enable_dump) {
    // hex dump
    fprintf(stderr, "[jit] fn=%u code_size=%zu\n", ctx.fnIndex, e.buf.size());
    for (size_t i = 0; i < e.buf.size(); ++i) {
      fprintf(stderr, "%02X ", (unsigned)e.buf.data()[i]);
      if ((i % 16) == 15) fprintf(stderr, "\n");
    }
    if ((e.buf.size() % 16) != 0) fprintf(stderr, "\n");
    // mini-disasm: just show addresses and bytes
    size_t base = 0;
    fprintf(stderr, "[jit] disasm (addr: byte)\n");
    for (size_t i = 0; i < e.buf.size(); ++i) {
      fprintf(stderr, "%04zu: %02X\n", base + i, (unsigned)e.buf.data()[i]);
    }
    // map: bc ip -> mc offset
    fprintf(stderr, "[jit] map bc_ip -> mc_off\n");
    for (auto &p : bc_to_mc) {
      fprintf(stderr, "  %u -> %zu\n", p.first, p.second);
    }
  }

  JitCompiled out;
  out.mem.reset(reinterpret_cast<uint8_t *>(ex.ptr));
  out.size  = e.buf.size();
  out.entry = reinterpret_cast<mplx::jit::JitEntryPtr>(ex.ptr);
  return out;
}
} 
