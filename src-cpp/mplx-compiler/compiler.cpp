#include "compiler.hpp"
#include <stdexcept>

namespace mplx {

void Compiler::emit_u8(uint8_t x){ bc_.code.push_back(x); }
void Compiler::emit_u32(uint32_t x){ for(int i=0;i<4;++i) bc_.code.push_back((uint8_t)((x>>(i*8))&0xFF)); }
void Compiler::write_u32_at(uint32_t pos, uint32_t val){
  if (pos+3 >= bc_.code.size()) return;
  bc_.code[pos+0] = (uint8_t)(val & 0xFF);
  bc_.code[pos+1] = (uint8_t)((val >> 8) & 0xFF);
  bc_.code[pos+2] = (uint8_t)((val >> 16) & 0xFF);
  bc_.code[pos+3] = (uint8_t)((val >> 24) & 0xFF);
}

uint32_t Compiler::addConst(long long v){ bc_.consts.push_back(v); return (uint32_t)bc_.consts.size()-1; }

uint16_t Compiler::localIndex(const std::string& name){
  for(int i=(int)scopes_.size()-1;i>=0;--i){ auto it=scopes_[i].find(name); if(it!=scopes_[i].end()) return it->second; }
  diags_.push_back("unknown variable: "+name); return 0;
}

void Compiler::compileExpr(const Expr* e){
  if(auto lit = dynamic_cast<const LiteralExpr*>(e)){
    auto idx = addConst(lit->value); emit_u8(OP_PUSH_CONST); emit_u32(idx); return;
  }
  if(auto v = dynamic_cast<const VarExpr*>(e)){
    auto idx = localIndex(v->name); emit_u8(OP_LOAD_LOCAL); emit_u32(idx); return;
  }
  if(auto u = dynamic_cast<const UnaryExpr*>(e)){
    compileExpr(u->rhs.get());
    if(u->op=="-") emit_u8(OP_NEG); else diags_.push_back("unsupported unary op: "+u->op);
    return;
  }
  if(auto b = dynamic_cast<const BinaryExpr*>(e)){
    compileExpr(b->lhs.get());
    compileExpr(b->rhs.get());
    const std::string& op=b->op;
    if(op=="+") emit_u8(OP_ADD); else if(op=="-") emit_u8(OP_SUB); else if(op=="*") emit_u8(OP_MUL); else if(op=="/") emit_u8(OP_DIV);
    else if(op=="==") emit_u8(OP_EQ); else if(op!="=" && op=="!=") emit_u8(OP_NE);
    else if(op=="<") emit_u8(OP_LT); else if(op=="<=") emit_u8(OP_LE); else if(op==">") emit_u8(OP_GT); else if(op==">=") emit_u8(OP_GE);
    else if(op=="!=") emit_u8(OP_NE);
    else diags_.push_back("unsupported binary op: "+op);
    return;
  }
  if(auto c = dynamic_cast<const CallExpr*>(e)){
    auto it = funcIndex_.find(c->callee);
    if(it==funcIndex_.end()){ diags_.push_back("unknown function: "+c->callee); emit_u8(OP_PUSH_CONST); emit_u32(addConst(0)); return; }
    for(auto& a : c->args) compileExpr(a.get());
    emit_u8(OP_CALL); emit_u32(it->second);
    return;
  }
  diags_.push_back("unknown expr kind");
}

void Compiler::compileStmt(const Stmt* s){
  if(auto let = dynamic_cast<const LetStmt*>(s)){
    auto& scope = scopes_.back();
    uint16_t idx = currentLocals_++;
    scope[let->name]=idx;
    compileExpr(let->init.get());
    emit_u8(OP_STORE_LOCAL); emit_u32(idx);
    return;
  }
  if(auto as = dynamic_cast<const AssignStmt*>(s)){
    auto idx = localIndex(as->name); compileExpr(as->value.get()); emit_u8(OP_STORE_LOCAL); emit_u32(idx); return;
  }
  if(auto ret = dynamic_cast<const ReturnStmt*>(s)){
    compileExpr(ret->value.get()); emit_u8(OP_RET); return;
  }
  if(auto ifs = dynamic_cast<const IfStmt*>(s)){
    compileExpr(ifs->cond.get());
    emit_u8(OP_JMP_IF_FALSE); auto jmpFalsePos = tell(); emit_u32(0);
    // then
    for(auto& st : ifs->thenS) compileStmt(st.get());
    emit_u8(OP_JMP); auto jmpEndPos = tell(); emit_u32(0);
    // patch false to else start
    write_u32_at(jmpFalsePos, tell());
    // else
    for(auto& st : ifs->elseS) compileStmt(st.get());
    // patch end to code end
    write_u32_at(jmpEndPos, tell());
    return;
  }
  if(auto es = dynamic_cast<const ExprStmt*>(s)){
    compileExpr(es->expr.get()); emit_u8(OP_POP); return;
  }
  diags_.push_back("unknown stmt kind");
}

void Compiler::compileFunction(const Function& f){
  FuncMeta meta; meta.name=f.name; meta.entry = tell(); meta.arity=(uint8_t)f.params.size();
  scopes_.push_back({}); currentArity_ = meta.arity; currentLocals_ = meta.arity;
  for(uint16_t p=0;p<meta.arity;++p){ scopes_.back()[f.params[p].name]=p; }
  for(auto& st : f.body) compileStmt(st.get());
  emit_u8(OP_PUSH_CONST); emit_u32(addConst(0)); // implicit 0
  emit_u8(OP_RET);
  meta.locals = currentLocals_;
  bc_.functions.push_back(meta);
  scopes_.pop_back();
}

CompileResult Compiler::compile(const Module& m){
  for(size_t i=0;i<m.functions.size();++i){ funcIndex_[m.functions[i].name]=(uint32_t)i; }
  for(auto& f : m.functions) compileFunction(f);
  emit_u8(OP_HALT);
  return CompileResult{std::move(bc_), std::move(diags_)};
}

} // namespace mplx