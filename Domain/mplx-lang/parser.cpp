#include "parser.hpp"
#include <sstream>

namespace mplx {

  const Token &Parser::peek() const {
    return t[i];
  }
  const Token &Parser::prev() const {
    return t[i - 1];
  }
  bool Parser::is_at_end() const {
    return peek().kind == TokenKind::Eof;
  }
  const Token &Parser::advance() {
    if (!is_at_end())
      ++i;
    return prev();
  }
  bool Parser::check(TokenKind k) const {
    return !is_at_end() && peek().kind == k;
  }
  bool Parser::match(TokenKind k) {
    if (check(k)) {
      advance();
      return true;
    }
    return false;
  }

  void Parser::error_here(const std::string &m) {
    std::ostringstream os;
    os << "[line " << peek().line << ":" << peek().col << "] " << m;
    diags.push_back(os.str());
  }

  Module Parser::parse() {
    Module m;
    while (!is_at_end()) {
      if (check(TokenKind::Eof))
        break;
      m.functions.push_back(parseFunction());
    }
    return m;
  }

  Function Parser::parseFunction() {
    if (!match(TokenKind::KwFn))
      error_here("expected 'fn'");
    if (!check(TokenKind::Identifier))
      error_here("expected function name");
    std::string name = advance().lexeme;
    if (!match(TokenKind::LParen))
      error_here("expected '('");
    auto params = parseParams();
    if (!match(TokenKind::RParen))
      error_here("expected ')'");
    if (!match(TokenKind::Arrow))
      error_here("expected '->'");
    std::string retType = check(TokenKind::Identifier) ? advance().lexeme : "i32";
    if (!match(TokenKind::LBrace))
      error_here("expected '{'");
    Function f;
    f.name       = name;
    f.params     = params;
    f.returnType = retType;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
      auto s = parseStmt();
      if (s)
        f.body.push_back(std::move(s));
    }
    if (!match(TokenKind::RBrace))
      error_here("expected '}'");
    return f;
  }

  std::vector<Param> Parser::parseParams() {
    std::vector<Param> ps;
    if (check(TokenKind::RParen))
      return ps;
    do {
      if (!check(TokenKind::Identifier)) {
        error_here("expected param name");
        break;
      }
      std::string nm = advance().lexeme;
      std::string tp = "i32";
      if (match(TokenKind::Colon)) {
        if (check(TokenKind::Identifier))
          tp = advance().lexeme;
      }
      ps.push_back(Param{nm, tp});
    } while (match(TokenKind::Comma));
    return ps;
  }

  std::unique_ptr<Stmt> Parser::parseStmt() {
    if (check(TokenKind::KwIf))
      return parseIf();
    if (check(TokenKind::KwLet))
      return parseLet();
    if (check(TokenKind::KwReturn))
      return parseReturn();
    return parseExprStmt();
  }

  std::unique_ptr<Stmt> Parser::parseIf() {
    advance(); // if
    if (!match(TokenKind::LParen))
      error_here("expected '(' after if");
    auto cond = expression();
    if (!match(TokenKind::RParen))
      error_here("expected ')' after condition");
    if (!match(TokenKind::LBrace))
      error_here("expected '{' after if");
    auto stmt = std::make_unique<IfStmt>();
    while (!check(TokenKind::RBrace) && !is_at_end())
      stmt->thenS.push_back(parseStmt());
    if (!match(TokenKind::RBrace))
      error_here("expected '}' after if-body");
    if (match(TokenKind::KwElse)) {
      if (!match(TokenKind::LBrace))
        error_here("expected '{' after else");
      while (!check(TokenKind::RBrace) && !is_at_end())
        stmt->elseS.push_back(parseStmt());
      if (!match(TokenKind::RBrace))
        error_here("expected '}' after else-body");
    }
    stmt->cond = std::move(cond);
    return stmt;
  }

  std::unique_ptr<Stmt> Parser::parseLet() {
    advance(); // let
    if (!check(TokenKind::Identifier)) {
      error_here("expected identifier after let");
      return nullptr;
    }
    std::string name = advance().lexeme;
    if (!match(TokenKind::Equal))
      error_here("expected '=' after let name");
    auto init = expression();
    if (!match(TokenKind::Semicolon))
      error_here("expected ';' after let");
    return std::make_unique<LetStmt>(name, std::move(init));
  }

  std::unique_ptr<Stmt> Parser::parseReturn() {
    advance(); // return
    auto v = expression();
    if (!match(TokenKind::Semicolon))
      error_here("expected ';' after return");
    return std::make_unique<ReturnStmt>(std::move(v));
  }

  std::unique_ptr<Stmt> Parser::parseExprStmt() {
    if (check(TokenKind::Identifier)) {
      Token id = peek();
      if (t.size() > i + 1 && t[i + 1].kind == TokenKind::Equal) {
        advance();
        match(TokenKind::Equal);
        auto val = expression();
        if (!match(TokenKind::Semicolon))
          error_here("expected ';' after assignment");
        return std::make_unique<AssignStmt>(id.lexeme, std::move(val));
      }
    }
    auto e = expression();
    if (!match(TokenKind::Semicolon))
      error_here("expected ';' after expression");
    return std::make_unique<ExprStmt>(std::move(e));
  }

  std::unique_ptr<Expr> Parser::expression() {
    return equality();
  }

  std::unique_ptr<Expr> Parser::equality() {
    auto e = comparison();
    while (check(TokenKind::EqEq) || check(TokenKind::BangEq)) {
      std::string op = advance().lexeme;
      auto r         = comparison();
      e              = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> Parser::comparison() {
    auto e = term();
    while (check(TokenKind::Lt) || check(TokenKind::Le) || check(TokenKind::Gt) || check(TokenKind::Ge)) {
      std::string op = advance().lexeme;
      auto r         = term();
      e              = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> Parser::term() {
    auto e = factor();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
      std::string op = advance().lexeme;
      auto r         = factor();
      e              = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> Parser::factor() {
    auto e = unary();
    while (check(TokenKind::Star) || check(TokenKind::Slash)) {
      std::string op = advance().lexeme;
      auto r         = unary();
      e              = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
    }
    return e;
  }

  std::unique_ptr<Expr> Parser::unary() {
    if (check(TokenKind::Minus)) {
      std::string op = advance().lexeme;
      auto r         = unary();
      return std::make_unique<UnaryExpr>(op, std::move(r));
    }
    return call();
  }

  std::unique_ptr<Expr> Parser::call() {
    auto e = primary();
    for (;;) {
      if (match(TokenKind::LParen)) {
        auto callee = dynamic_cast<VarExpr *>(e.get());
        if (!callee)
          return e; // not a callable
        auto call = std::make_unique<CallExpr>(callee->name);
        if (!check(TokenKind::RParen)) {
          do {
            call->args.push_back(expression());
          } while (match(TokenKind::Comma));
        }
        if (!match(TokenKind::RParen))
          error_here("expected ')' after call");
        e = std::move(call);
      } else {
        break;
      }
    }
    return e;
  }

  std::unique_ptr<Expr> Parser::primary() {
    if (check(TokenKind::Number)) {
      long long v = std::stoll(advance().lexeme);
      return std::make_unique<LiteralExpr>(v);
    }
    if (check(TokenKind::Identifier)) {
      return std::make_unique<VarExpr>(advance().lexeme);
    }
    if (match(TokenKind::LParen)) {
      auto e = expression();
      if (!match(TokenKind::RParen))
        error_here("expected ')'");
      return e;
    }
    error_here("unexpected token in expression");
    return std::make_unique<LiteralExpr>(0);
  }

} // namespace mplx
