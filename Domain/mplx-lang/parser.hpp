#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <string>
#include <vector>

namespace mplx {

  class Parser {
  public:
    explicit Parser(std::vector<Token> toks) : t(std::move(toks)) {}
    Module parse();
    const std::vector<std::string> &diagnostics() const {
      return diags;
    }

  private:
    bool match(TokenKind k);
    bool check(TokenKind k) const;
    const Token &advance();
    const Token &peek() const;
    const Token &prev() const;
    bool is_at_end() const;
    void error_here(const std::string &m);

    // grammar
    Function parseFunction();
    std::vector<Param> parseParams();
    std::unique_ptr<Stmt> parseStmt();
    std::unique_ptr<Stmt> parseIf();
    std::unique_ptr<Stmt> parseWhile();
    std::unique_ptr<Stmt> parseLet();
    std::unique_ptr<Stmt> parseReturn();
    std::unique_ptr<Stmt> parseExprStmt();

    std::unique_ptr<Expr> expression();
    std::unique_ptr<Expr> equality();
    std::unique_ptr<Expr> comparison();
    std::unique_ptr<Expr> term();
    std::unique_ptr<Expr> factor();
    std::unique_ptr<Expr> unary();
    std::unique_ptr<Expr> call();
    std::unique_ptr<Expr> primary();

    std::vector<Token> t;
    size_t i{0};
    std::vector<std::string> diags;
  };

} // namespace mplx