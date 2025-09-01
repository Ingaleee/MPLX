#include "vm.hpp"
#include <cstring>

namespace mplx {

static uint32_t read_u32(const std::vector<uint8_t>& c, uint32_t& ip){
  uint32_t v = (uint32_t)c[ip] | ((uint32_t)c[ip+1]<<8) | ((uint32_t)c[ip+2]<<16) | ((uint32_t)c[ip+3]<<24); ip+=4; return v;
}

long long VM::run(const std::string& entry){
  std::unordered_map<std::string,uint32_t> name2idx;
  for(uint32_t i=0;i<bc_.functions.size();++i) name2idx[bc_.functions[i].name]=i;
  auto it = name2idx.find(entry);
  if(it==name2idx.end()) throw std::runtime_error("entry function not found");
  auto fn = bc_.functions[it->second];

  // initial frame (return ip = code end -> HALT)
  frames_.push_back(CallFrame{ (uint32_t)bc_.code.size()-1, it->second, 0, fn.arity, fn.locals });
  ip_ = fn.entry;

  while(true){
    auto op = (Op)bc_.code[ip_++];
    switch(op){
      case OP_PUSH_CONST: { uint32_t idx = read_u32(bc_.code, ip_); push(bc_.consts[idx]); break; }
      case OP_LOAD_LOCAL: { uint32_t idx = read_u32(bc_.code, ip_); auto bp = frames_.back().bp; push(stack_[bp+idx].i); break; }
      case OP_STORE_LOCAL:{ uint32_t idx = read_u32(bc_.code, ip_); auto bp = frames_.back().bp; long long v = pop(); if(bp+idx>=stack_.size()) stack_.resize(bp+idx+1); stack_[bp+idx].i = v; push(v); break; }
      case OP_ADD: { auto b=pop(); auto a=pop(); push(a+b); break; }
      case OP_SUB: { auto b=pop(); auto a=pop(); push(a-b); break; }
      case OP_MUL: { auto b=pop(); auto a=pop(); push(a*b); break; }
      case OP_DIV: { auto b=pop(); auto a=pop(); push(a/b); break; }
      case OP_NEG: { auto a=pop(); push(-a); break; }
      case OP_EQ: { auto b=pop(); auto a=pop(); push(a==b); break; }
      case OP_NE: { auto b=pop(); auto a=pop(); push(a!=b); break; }
      case OP_LT: { auto b=pop(); auto a=pop(); push(a<b); break; }
      case OP_LE: { auto b=pop(); auto a=pop(); push(a<=b); break; }
      case OP_GT: { auto b=pop(); auto a=pop(); push(a>b); break; }
      case OP_GE: { auto b=pop(); auto a=pop(); push(a>=b); break; }
      case OP_JMP: { uint32_t dst = read_u32(bc_.code, ip_); ip_ = dst; break; }
      case OP_JMP_IF_FALSE: { uint32_t dst = read_u32(bc_.code, ip_); auto c = pop(); if(!c) ip_=dst; break; }
      case OP_CALL: {
        uint32_t idx = read_u32(bc_.code, ip_);
        auto callee = bc_.functions[idx];
        uint32_t bp = (uint32_t)(stack_.size() - callee.arity);
        uint32_t ret_ip = ip_; // return to next instruction after CALL
        // pre-reserve locals to minimize resizes
        if (stack_.size() < (size_t)(bp + callee.locals)) stack_.resize((size_t)(bp + callee.locals));
        frames_.push_back(CallFrame{ret_ip, idx, bp, callee.arity, callee.locals});
        ip_ = callee.entry;
        break;
      }
      case OP_RET: {
        long long ret = pop();
        auto frame = frames_.back(); frames_.pop_back();
        // drop stack to base pointer
        stack_.resize(frame.bp);
        push(ret);
        if(frames_.empty()) return ret;
        ip_ = frame.ip;
        break;
      }
      case OP_POP: {
        // fast-path: if next instruction is RET, skip actual pop
        Op next = (ip_ < bc_.code.size()) ? (Op)bc_.code[ip_] : OP_HALT;
        if (next == OP_RET) { break; }
        (void)pop();
        break;
      }
      case OP_HALT: return pop();
      default: throw std::runtime_error("unknown opcode");
    }
  }
}

} // namespace mplx