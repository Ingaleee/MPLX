#include "jit_compiler.hpp"
#include "../mplx-compiler/bytecode.hpp"
#include "jit_runtime.hpp"
#include "platform.hpp"
#include <cstring>

namespace mplx::jit {

  static uint32_t read_u32(const std::vector<uint8_t> &code, uint32_t &ip) {
    uint32_t v = (uint32_t)code[ip] | ((uint32_t)code[ip + 1] << 8) | ((uint32_t)code[ip + 2] << 16) | ((uint32_t)code[ip + 3] << 24);
    ip += 4;
    return v;
  }

  std::optional<JitCompiled> JitCompiler::compileFunction(const CompileCtx &ctx) {
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

    X64Emitter e;
    e.prologue();

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
        if (gop == OP_JMP) {
          uint32_t dst = read_u32(bc.code, gip);
          int lid      = ip_to_label[dst];
          e.jmp_label(lid);
          continue;
        }
        if (gop == OP_JMP_IF_FALSE) {
          uint32_t dst = read_u32(bc.code, gip);
          int lid      = ip_to_label[dst];
          // Convention for now: test rax,rax (result of last calc) then jz
          e.test_rax_rax();
          e.jz_label(lid);
          continue;
        }
        if (gop == OP_JMP_IF_TRUE) {
          uint32_t dst = read_u32(bc.code, gip);
          int lid      = ip_to_label[dst];
          e.test_rax_rax();
          e.jnz_label(lid);
          continue;
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

    // MVP: until full translation is ready, return computed constant/stack-top
    uint64_t result = st.empty() ? 0ull : (uint64_t)st.back();
    e.mov_rax_imm(result);
    e.epilogue();
    e.ret();
    e.finalize_fixups();

    auto ex = plat::alloc_executable(e.buf.size());
    if (!ex.ptr)
      return std::nullopt;
    std::memcpy(ex.ptr, e.buf.data(), e.buf.size());
    plat::flush_icache(ex.ptr, e.buf.size());

    JitCompiled out;
    out.mem.reset(reinterpret_cast<uint8_t *>(ex.ptr));
    out.size  = e.buf.size();
    out.entry = reinterpret_cast<JitEntryPtr>(ex.ptr);
    return out;
  }

} // namespace mplx::jit
