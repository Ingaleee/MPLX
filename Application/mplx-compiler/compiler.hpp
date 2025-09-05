#pragma once
#include "../mplx-lang/ast.hpp"
#include "bytecode.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace mplx {

  struct CompileResult {
    Bytecode bc;
    std::vector<std::string> diags;
  };

  class Compiler {
  public:
    CompileResult compile(const Module &m);

  private:
    void emit_u8(uint8_t x);
    void emit_u32(uint32_t x);
    void write_u32_at(uint32_t pos, uint32_t val);
    uint32_t tell() const {
      return (uint32_t)bc_.code.size();
    }

    // functions
    void compileFunction(const Function &f);
    void compileStmt(const Stmt *s);
    void compileExpr(const Expr *e);

    // helpers
    uint32_t addConst(long long v);
    uint16_t localIndex(const std::string &name);

    Bytecode bc_;
    std::vector<std::string> diags_;
    std::unordered_map<std::string, uint32_t> funcIndex_;
    std::vector<std::unordered_map<std::string, uint16_t>> scopes_;
    uint8_t currentArity_{0};
    uint16_t currentLocals_{0};
  };

} // namespace mplx