# --- Bootstrap MPLX (C++ ядро + TS LSP) ---
$ErrorActionPreference = "Stop"

# Root, git, ignores
if (!(Test-Path -LiteralPath .git)) { git init -q }
@'
build/
.vs/
.vscode/
CMakeSettings.json
CMakeUserPresets.json
**/*.user
**/*.suo
# Node
node_modules/
.pnpm-store/
# Logs
*.log
# OS
.DS_Store
Thumbs.db
'@ | Out-File -Encoding utf8 .gitignore -NoNewline

@'
# MPLX

Minimal language -> bytecode compiler, stack VM (C++20), CLI, tests, and TS-based LSP/VS Code extension.
'@ | Out-File -Encoding utf8 README.md -NoNewline

# Root CMake + presets + vcpkg manifest
@'
cmake_minimum_required(VERSION 3.26)
project(mplx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src-cpp/mplx-lang)
add_subdirectory(src-cpp/mplx-compiler)
add_subdirectory(src-cpp/mplx-vm)
add_subdirectory(src-cpp/tools/mplx)
add_subdirectory(tests-cpp)
'@ | Out-File -Encoding utf8 CMakeLists.txt -NoNewline

@'
{
  "name": "mplx",
  "version-string": "0.1.0",
  "dependencies": [
    "fmt",
    "nlohmann-json",
    "gtest"
  ]
}
'@ | Out-File -Encoding utf8 vcpkg.json -NoNewline

@'
{
  "version": 5,
  "configurePresets": [
    {
      "name": "dev",
      "generator": "Ninja",
      "binaryDir": "build/dev",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_TOOLCHAIN_FILE": "${env.VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      }
    }
  ],
  "buildPresets": [
    { "name": "dev", "configurePreset": "dev" }
  ]
}
'@ | Out-File -Encoding utf8 CMakePresets.json -NoNewline

# Directories
$dirs = @(
 "src-cpp/mplx-lang",
 "src-cpp/mplx-compiler",
 "src-cpp/mplx-vm",
 "src-cpp/tools/mplx",
 "tests-cpp",
 "examples",
 "src/Mplx.Lsp/src",
 "src/vscode-mplx/src"
)
$dirs | ForEach-Object { New-Item -Force -ItemType Directory $_ | Out-Null }

# --------- src-cpp/mplx-lang ----------
@'
add_library(mplx-lang
  lexer.cpp
  parser.cpp
)

find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(mplx-lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-lang PUBLIC nlohmann_json::nlohmann_json)
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/CMakeLists.txt -NoNewline

@'
#pragma once
#include <string>
#include <cstdint>

namespace mplx {

enum class TokenKind {
  Eof,
  Identifier,
  Number,
  KwFn, KwLet, KwReturn, KwIf, KwElse,
  LParen, RParen, LBrace, RBrace,
  Comma, Semicolon, Colon,
  Arrow, // ->
  Plus, Minus, Star, Slash,
  Equal, // =
  EqEq, BangEq,
  Lt, Le, Gt, Ge,
};

struct Token {
  TokenKind kind{};
  std::string lexeme{};
  std::size_t line{1};
  std::size_t col{1};
};

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/token.hpp -NoNewline

@'
#pragma once
#include "token.hpp"
#include <string>
#include <vector>

namespace mplx {

class Lexer {
public:
  explicit Lexer(std::string src) : src_(std::move(src)) {}
  std::vector<Token> Lex();

private:
  char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
  char get() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }
  void skip_ws();
  Token make(TokenKind k, std::size_t start, std::size_t line, std::size_t col, std::size_t len);

  std::string src_;
  std::size_t pos_{0};
  std::size_t line_{1};
  std::size_t col_{1};
};

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/lexer.hpp -NoNewline

@'
#include "lexer.hpp"
#include <cctype>
#include <unordered_map>

namespace mplx {

static const std::unordered_map<std::string, TokenKind> kKeywords = {
  {"fn", TokenKind::KwFn},
  {"let", TokenKind::KwLet},
  {"return", TokenKind::KwReturn},
  {"if", TokenKind::KwIf},
  {"else", TokenKind::KwElse},
};

void Lexer::skip_ws() {
  for (;;) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') { get(); ++col_; }
    else if (c == '\n') { get(); ++line_; col_ = 1; }
    else if (c == '/' && pos_+1 < src_.size() && src_[pos_+1] == '/') {
      while (peek() != '\n' && peek() != '\0') { get(); }
    } else {
      break;
    }
  }
}

Token Lexer::make(TokenKind k, std::size_t start, std::size_t line, std::size_t col, std::size_t len) {
  Token t; t.kind = k; t.lexeme = src_.substr(start, len); t.line = line; t.col = col; return t;
}

std::vector<Token> Lexer::Lex() {
  std::vector<Token> out;
  while (true) {
    skip_ws();
    std::size_t tok_line = line_, tok_col = col_;
    char c = peek();
    if (c == '\0') { out.push_back(Token{TokenKind::Eof, "", tok_line, tok_col}); break; }

    if (std::isalpha((unsigned char)c) || c == '_') {
      std::size_t start = pos_; std::size_t col0 = col_;
      while (std::isalnum((unsigned char)peek()) || peek()=='_') { get(); ++col_; }
      std::string s = src_.substr(start, pos_-start);
      auto it = kKeywords.find(s);
      out.push_back(make(it==kKeywords.end()?TokenKind::Identifier:it->second, start, tok_line, col0, pos_-start));
      continue;
    }

    if (std::isdigit((unsigned char)c)) {
      std::size_t start = pos_; std::size_t col0 = col_;
      while (std::isdigit((unsigned char)peek())) { get(); ++col_; }
      out.push_back(make(TokenKind::Number, start, tok_line, col0, pos_-start));
      continue;
    }

    // two-char tokens
    if (c=='-' && pos_+1<src_.size() && src_[pos_+1]=='>') { get(); get(); out.push_back(Token{TokenKind::Arrow, "->", tok_line, tok_col}); col_+=2; continue; }
    if (c=='=' && pos_+1<src_.size() && src_[pos_+1]=='=') { get(); get(); out.push_back(Token{TokenKind::EqEq, "==", tok_line, tok_col}); col_+=2; continue; }
    if (c=='!' && pos_+1<src_.size() && src_[pos_+1]=='=') { get(); get(); out.push_back(Token{TokenKind::BangEq, "!=", tok_line, tok_col}); col_+=2; continue; }
    if (c=='<' && pos_+1<src_.size() && src_[pos_+1]=='=') { get(); get(); out.push_back(Token{TokenKind::Le, "<=", tok_line, tok_col}); col_+=2; continue; }
    if (c=='>' && pos_+1<src_.size() && src_[pos_+1]=='=') { get(); get(); out.push_back(Token{TokenKind::Ge, ">=", tok_line, tok_col}); col_+=2; continue; }

    // single-char
    get();
    switch (c) {
      case '(': out.push_back(Token{TokenKind::LParen, "(", tok_line, tok_col}); ++col_; break;
      case ')': out.push_back(Token{TokenKind::RParen, ")", tok_line, tok_col}); ++col_; break;
      case '{': out.push_back(Token{TokenKind::LBrace, "{", tok_line, tok_col}); ++col_; break;
      case '}': out.push_back(Token{TokenKind::RBrace, "}", tok_line, tok_col}); ++col_; break;
      case ',': out.push_back(Token{TokenKind::Comma, ",", tok_line, tok_col}); ++col_; break;
      case ';': out.push_back(Token{TokenKind::Semicolon, ";", tok_line, tok_col}); ++col_; break;
      case ':': out.push_back(Token{TokenKind::Colon, ":", tok_line, tok_col}); ++col_; break;
      case '+': out.push_back(Token{TokenKind::Plus, "+", tok_line, tok_col}); ++col_; break;
      case '-': out.push_back(Token{TokenKind::Minus, "-", tok_line, tok_col}); ++col_; break;
      case '*': out.push_back(Token{TokenKind::Star, "*", tok_line, tok_col}); ++col_; break;
      case '/': out.push_back(Token{TokenKind::Slash, "/", tok_line, tok_col}); ++col_; break;
      case '=': out.push_back(Token{TokenKind::Equal, "=", tok_line, tok_col}); ++col_; break;
      case '<': out.push_back(Token{TokenKind::Lt, "<", tok_line, tok_col}); ++col_; break;
      case '>': out.push_back(Token{TokenKind::Gt, ">", tok_line, tok_col}); ++col_; break;
      default:  ++col_; break;
    }
  }
  return out;
}

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/lexer.cpp -NoNewline

@'
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace mplx {

struct Expr { virtual ~Expr() = default; };

struct LiteralExpr : Expr { long long value; explicit LiteralExpr(long long v):value(v){} };
struct VarExpr : Expr { std::string name; explicit VarExpr(std::string n):name(std::move(n)){} };
struct UnaryExpr : Expr { std::string op; std::unique_ptr<Expr> rhs; UnaryExpr(std::string o, std::unique_ptr<Expr> r):op(std::move(o)),rhs(std::move(r)){} };
struct BinaryExpr : Expr { std::string op; std::unique_ptr<Expr> lhs, rhs; BinaryExpr(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r):op(std::move(o)),lhs(std::move(l)),rhs(std::move(r)){} };
struct CallExpr : Expr { std::string callee; std::vector<std::unique_ptr<Expr>> args; explicit CallExpr(std::string c):callee(std::move(c)){} };

struct Stmt { virtual ~Stmt() = default; };
struct LetStmt : Stmt { std::string name; std::unique_ptr<Expr> init; LetStmt(std::string n,std::unique_ptr<Expr> i):name(std::move(n)),init(std::move(i)){} };
struct AssignStmt : Stmt { std::string name; std::unique_ptr<Expr> value; AssignStmt(std::string n,std::unique_ptr<Expr> v):name(std::move(n)),value(std::move(v)){} };
struct ReturnStmt : Stmt { std::unique_ptr<Expr> value; explicit ReturnStmt(std::unique_ptr<Expr> v):value(std::move(v)){} };
struct ExprStmt : Stmt { std::unique_ptr<Expr> expr; explicit ExprStmt(std::unique_ptr<Expr> e):expr(std::move(e)){} };
struct IfStmt : Stmt { std::unique_ptr<Expr> cond; std::vector<std::unique_ptr<Stmt>> thenS; std::vector<std::unique_ptr<Stmt>> elseS; };

struct Param { std::string name; std::string typeName; };

struct Function {
  std::string name;
  std::vector<Param> params;
  std::string returnType;
  std::vector<std::unique_ptr<Stmt>> body;
};

struct Module { std::vector<Function> functions; };

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/ast.hpp -NoNewline

@'
#pragma once
#include "token.hpp"
#include "ast.hpp"
#include <vector>
#include <string>

namespace mplx {

class Parser {
public:
  explicit Parser(std::vector<Token> toks) : t(std::move(toks)) {}
  Module parse();
  const std::vector<std::string>& diagnostics() const { return diags; }

private:
  bool match(TokenKind k);
  bool check(TokenKind k) const;
  const Token& advance();
  const Token& peek() const;
  const Token& prev() const;
  bool is_at_end() const;
  void error_here(const std::string& m);

  // grammar
  Function parseFunction();
  std::vector<Param> parseParams();
  std::unique_ptr<Stmt> parseStmt();
  std::unique_ptr<Stmt> parseIf();
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

  std::vector<Token> t; size_t i{0};
  std::vector<std::string> diags;
};

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/parser.hpp -NoNewline

@'
#include "parser.hpp"
#include <sstream>

namespace mplx {

const Token& Parser::peek() const { return t[i]; }
const Token& Parser::prev() const { return t[i-1]; }
bool Parser::is_at_end() const { return peek().kind == TokenKind::Eof; }
const Token& Parser::advance() { if (!is_at_end()) ++i; return prev(); }
bool Parser::check(TokenKind k) const { return !is_at_end() && peek().kind == k; }
bool Parser::match(TokenKind k){ if(check(k)){ advance(); return true;} return false; }

void Parser::error_here(const std::string& m){ std::ostringstream os; os << "[line "<<peek().line<<":"<<peek().col<<"] "<<m; diags.push_back(os.str()); }

Module Parser::parse(){ Module m; while(!is_at_end()) { if(check(TokenKind::Eof)) break; m.functions.push_back(parseFunction()); } return m; }

Function Parser::parseFunction(){
  if(!match(TokenKind::KwFn)) error_here("expected 'fn'");
  if(!check(TokenKind::Identifier)) error_here("expected function name");
  std::string name = advance().lexeme;
  if(!match(TokenKind::LParen)) error_here("expected '('");
  auto params = parseParams();
  if(!match(TokenKind::RParen)) error_here("expected ')'");
  if(!match(TokenKind::Arrow)) error_here("expected '->'");
  std::string retType = check(TokenKind::Identifier)? advance().lexeme : "i32";
  if(!match(TokenKind::LBrace)) error_here("expected '{'");
  Function f{.name=name,.params=params,.returnType=retType};
  while(!check(TokenKind::RBrace) && !is_at_end()){
    auto s = parseStmt();
    if(s) f.body.push_back(std::move(s));
  }
  if(!match(TokenKind::RBrace)) error_here("expected '}'");
  return f;
}

std::vector<Param> Parser::parseParams(){
  std::vector<Param> ps;
  if(check(TokenKind::RParen)) return ps;
  do{
    if(!check(TokenKind::Identifier)) { error_here("expected param name"); break; }
    std::string nm = advance().lexeme;
    std::string tp = "i32";
    if(match(TokenKind::Colon)) { if(check(TokenKind::Identifier)) tp=advance().lexeme; }
    ps.push_back(Param{nm,tp});
  } while(match(TokenKind::Comma));
  return ps;
}

std::unique_ptr<Stmt> Parser::parseStmt(){
  if(check(TokenKind::KwIf)) return parseIf();
  if(check(TokenKind::KwLet)) return parseLet();
  if(check(TokenKind::KwReturn)) return parseReturn();
  return parseExprStmt();
}

std::unique_ptr<Stmt> Parser::parseIf(){
  advance(); // if
  if(!match(TokenKind::LParen)) error_here("expected '(' after if");
  auto cond = expression();
  if(!match(TokenKind::RParen)) error_here("expected ')' after condition");
  if(!match(TokenKind::LBrace)) error_here("expected '{' after if");
  auto stmt = std::make_unique<IfStmt>();
  while(!check(TokenKind::RBrace) && !is_at_end()) stmt->thenS.push_back(parseStmt());
  if(!match(TokenKind::RBrace)) error_here("expected '}' after if-body");
  if(match(TokenKind::KwElse)){
    if(!match(TokenKind::LBrace)) error_here("expected '{' after else");
    while(!check(TokenKind::RBrace) && !is_at_end()) stmt->elseS.push_back(parseStmt());
    if(!match(TokenKind::RBrace)) error_here("expected '}' after else-body");
  }
  stmt->cond = std::move(cond);
  return stmt;
}

std::unique_ptr<Stmt> Parser::parseLet(){
  advance(); // let
  if(!check(TokenKind::Identifier)) { error_here("expected identifier after let"); return nullptr; }
  std::string name = advance().lexeme;
  if(!match(TokenKind::Equal)) error_here("expected '=' after let name");
  auto init = expression();
  if(!match(TokenKind::Semicolon)) error_here("expected ';' after let");
  return std::make_unique<LetStmt>(name, std::move(init));
}

std::unique_ptr<Stmt> Parser::parseReturn(){
  advance(); // return
  auto v = expression();
  if(!match(TokenKind::Semicolon)) error_here("expected ';' after return");
  return std::make_unique<ReturnStmt>(std::move(v));
}

std::unique_ptr<Stmt> Parser::parseExprStmt(){
  if(check(TokenKind::Identifier)){
    Token id = peek();
    if(t.size()>i+1 && t[i+1].kind==TokenKind::Equal){
      advance(); match(TokenKind::Equal);
      auto val = expression();
      if(!match(TokenKind::Semicolon)) error_here("expected ';' after assignment");
      return std::make_unique<AssignStmt>(id.lexeme, std::move(val));
    }
  }
  auto e = expression();
  if(!match(TokenKind::Semicolon)) error_here("expected ';' after expression");
  return std::make_unique<ExprStmt>(std::move(e));
}

std::unique_ptr<Expr> Parser::expression(){ return equality(); }

std::unique_ptr<Expr> Parser::equality(){
  auto e = comparison();
  while(check(TokenKind::EqEq) || check(TokenKind::BangEq)){
    std::string op = advance().lexeme;
    auto r = comparison();
    e = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
  }
  return e;
}

std::unique_ptr<Expr> Parser::comparison(){
  auto e = term();
  while(check(TokenKind::Lt)||check(TokenKind::Le)||check(TokenKind::Gt)||check(TokenKind::Ge)){
    std::string op = advance().lexeme;
    auto r = term();
    e = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
  }
  return e;
}

std::unique_ptr<Expr> Parser::term(){
  auto e = factor();
  while(check(TokenKind::Plus) || check(TokenKind::Minus)){
    std::string op = advance().lexeme;
    auto r = factor();
    e = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
  }
  return e;
}

std::unique_ptr<Expr> Parser::factor(){
  auto e = unary();
  while(check(TokenKind::Star) || check(TokenKind::Slash)){
    std::string op = advance().lexeme;
    auto r = unary();
    e = std::make_unique<BinaryExpr>(std::move(e), op, std::move(r));
  }
  return e;
}

std::unique_ptr<Expr> Parser::unary(){
  if(check(TokenKind::Minus)){
    std::string op = advance().lexeme; auto r = unary();
    return std::make_unique<UnaryExpr>(op, std::move(r));
  }
  return call();
}

std::unique_ptr<Expr> Parser::call(){
  auto e = primary();
  for(;;){
    if(match(TokenKind::LParen)){
      auto callee = dynamic_cast<VarExpr*>(e.get());
      if(!callee) return e; // not a callable
      auto call = std::make_unique<CallExpr>(callee->name);
      if(!check(TokenKind::RParen)){
        do { call->args.push_back(expression()); } while(match(TokenKind::Comma));
      }
      if(!match(TokenKind::RParen)) error_here("expected ')' after call");
      e = std::move(call);
    } else {
      break;
    }
  }
  return e;
}

std::unique_ptr<Expr> Parser::primary(){
  if(check(TokenKind::Number)) { long long v = std::stoll(advance().lexeme); return std::make_unique<LiteralExpr>(v); }
  if(check(TokenKind::Identifier)) { return std::make_unique<VarExpr>(advance().lexeme); }
  if(match(TokenKind::LParen)) { auto e = expression(); if(!match(TokenKind::RParen)) error_here("expected ')'"); return e; }
  error_here("unexpected token in expression");
  return std::make_unique<LiteralExpr>(0);
}

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-lang/parser.cpp -NoNewline

# --------- src-cpp/mplx-compiler ----------
@'
add_library(mplx-compiler
  compiler.cpp
)

target_include_directories(mplx-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-lang)
target_link_libraries(mplx-compiler PUBLIC mplx-lang)
'@ | Out-File -Encoding utf8 src-cpp/mplx-compiler/CMakeLists.txt -NoNewline

@'
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mplx {

enum Op : uint8_t {
  OP_PUSH_CONST,
  OP_LOAD_LOCAL,
  OP_STORE_LOCAL,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV,
  OP_NEG,
  OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
  OP_JMP,
  OP_JMP_IF_FALSE,
  OP_CALL,
  OP_RET,
  OP_POP,
  OP_HALT
};

struct FuncMeta { std::string name; uint32_t entry{0}; uint8_t arity{0}; uint16_t locals{0}; };

struct Bytecode {
  std::vector<uint8_t> code;
  std::vector<long long> consts;
  std::vector<FuncMeta> functions;
};

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-compiler/bytecode.hpp -NoNewline

@'
#pragma once
#include "../mplx-lang/ast.hpp"
#include "bytecode.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace mplx {

struct CompileResult { Bytecode bc; std::vector<std::string> diags; };

class Compiler {
public:
  CompileResult compile(const Module& m);
private:
  void emit_u8(uint8_t x); void emit_u32(uint32_t x);
  void write_u32_at(uint32_t pos, uint32_t val);
  uint32_t tell() const { return (uint32_t)bc_.code.size(); }

  // functions
  void compileFunction(const Function& f);
  void compileStmt(const Stmt* s);
  void compileExpr(const Expr* e);

  // helpers
  uint32_t addConst(long long v);
  uint16_t localIndex(const std::string& name);

  Bytecode bc_;
  std::vector<std::string> diags_;
  std::unordered_map<std::string, uint32_t> funcIndex_;
  std::vector<std::unordered_map<std::string,uint16_t>> scopes_;
  uint8_t currentArity_{0};
  uint16_t currentLocals_{0};
};

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-compiler/compiler.hpp -NoNewline

@'
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
'@ | Out-File -Encoding utf8 src-cpp/mplx-compiler/compiler.cpp -NoNewline

# --------- src-cpp/mplx-vm ----------
@'
add_library(mplx-vm vm.cpp)

target_include_directories(mplx-vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler)
target_link_libraries(mplx-vm PUBLIC mplx-compiler)
'@ | Out-File -Encoding utf8 src-cpp/mplx-vm/CMakeLists.txt -NoNewline

@'
#pragma once
#include "../mplx-compiler/bytecode.hpp"
#include <vector>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace mplx {

struct VMValue { long long i; };

struct CallFrame { uint32_t ip; uint32_t fn; uint32_t bp; uint8_t arity; uint16_t locals; };

class VM {
public:
  explicit VM(const Bytecode& bc) : bc_(bc) {}
  long long run(const std::string& entry = "main");

private:
  const Bytecode& bc_;
  std::vector<VMValue> stack_;
  std::vector<CallFrame> frames_;
  uint32_t ip_{0};

  void push(long long x){ stack_.push_back(VMValue{x}); }
  long long pop(){ auto v=stack_.back().i; stack_.pop_back(); return v; }
};

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-vm/vm.hpp -NoNewline

@'
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
      case OP_POP: { (void)pop(); break; }
      case OP_HALT: return pop();
      default: throw std::runtime_error("unknown opcode");
    }
  }
}

} // namespace mplx
'@ | Out-File -Encoding utf8 src-cpp/mplx-vm/vm.cpp -NoNewline

# --------- src-cpp/tools/mplx ----------
@'
add_executable(mplx
  main.cpp
)

find_package(fmt CONFIG REQUIRED)

target_include_directories(mplx PRIVATE ../mplx-lang ../mplx-compiler ../mplx-vm)
target_link_libraries(mplx PRIVATE fmt::fmt mplx-lang mplx-compiler mplx-vm)
'@ | Out-File -Encoding utf8 src-cpp/tools/mplx/CMakeLists.txt -NoNewline

@'
#include "../../mplx-lang/lexer.hpp"
#include "../../mplx-lang/parser.hpp"
#include "../../mplx-compiler/compiler.hpp"
#include "../../mplx-vm/vm.hpp"
#include <fmt/core.h>
#include <fstream>
#include <sstream>

int main(int argc, char** argv){
  std::string src;
  if(argc>=3 && std::string(argv[1])=="--run"){
    std::ifstream ifs(argv[2]);
    std::stringstream ss; ss << ifs.rdbuf(); src = ss.str();
  } else {
    src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  }

  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();
  if(!ps.diagnostics().empty()){
    for(auto& d : ps.diagnostics()) fmt::print("parser: {}\n", d);
    return 1;
  }
  mplx::Compiler c;
  auto res = c.compile(mod);
  if(!res.diags.empty()){
    for(auto& d : res.diags) fmt::print("compile: {}\n", d);
    return 1;
  }
  mplx::VM vm(res.bc);
  auto v = vm.run("main");
  fmt::print("{}\n", v);
  return 0;
}
'@ | Out-File -Encoding utf8 src-cpp/tools/mplx/main.cpp -NoNewline

# --------- tests-cpp ----------
@'
add_executable(mplx-tests
  lexer_tests.cpp
)

find_package(GTest CONFIG REQUIRED)
target_link_libraries(mplx-tests PRIVATE GTest::gtest_main mplx-lang)
include(GoogleTest)
gtest_discover_tests(mplx-tests)
'@ | Out-File -Encoding utf8 tests-cpp/CMakeLists.txt -NoNewline

@'
#include <gtest/gtest.h>
#include "../src-cpp/mplx-lang/lexer.hpp"

TEST(Lexer, Basic){
  mplx::Lexer lx("fn main() { let x = 1 + 2; }");
  auto toks = lx.Lex();
  ASSERT_GE(toks.size(), 5);
}
'@ | Out-File -Encoding utf8 tests-cpp/lexer_tests.cpp -NoNewline

# --------- examples ----------
@'
fn main() -> i32 {
  let x = 1 + 2 * 3;
  if (x > 5) {
    x = x - 1;
  } else {
    x = x + 1;
  }
  return x;
}
'@ | Out-File -Encoding utf8 examples/hello.mplx -NoNewline

# --------- LSP (TS) skeleton ----------
@'
{
  "name": "mplx-lsp",
  "version": "0.1.0",
  "type": "module",
  "private": true,
  "scripts": {
    "build": "tsc -p ."
  },
  "devDependencies": {
    "typescript": "^5.4.0",
    "vscode-languageserver": "^9.0.1"
  }
}
'@ | Out-File -Encoding utf8 src/Mplx.Lsp/package.json -NoNewline

@'
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ES2020",
    "outDir": "dist",
    "strict": true,
    "moduleResolution": "bundler",
    "esModuleInterop": true,
    "skipLibCheck": true
  },
  "include": ["src"]
}
'@ | Out-File -Encoding utf8 src/Mplx.Lsp/tsconfig.json -NoNewline

@'
import { createConnection, ProposedFeatures, InitializeParams, InitializeResult, TextDocuments, TextDocument } from 'vscode-languageserver'

const connection = createConnection(ProposedFeatures.all)
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument)

connection.onInitialize((_params: InitializeParams): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync: 1,
    }
  }
})

documents.onDidOpen(e => {
  connection.console.log(`Opened ${e.document.uri}`)
})

documents.listen(connection)
connection.listen()
'@ | Out-File -Encoding utf8 src/Mplx.Lsp/src/server.ts -NoNewline

# --------- VS Code extension ----------
@'
{
  "name": "mplx",
  "displayName": "MPLX Language",
  "version": "0.1.0",
  "engines": { "vscode": "^1.87.0" },
  "publisher": "you",
  "categories": ["Programming Languages"],
  "activationEvents": ["onLanguage:mplx"],
  "main": "dist/extension.js",
  "contributes": {
    "languages": [{
      "id": "mplx",
      "extensions": [".mplx"],
      "aliases": ["MPLX"],
      "configuration": "./language-configuration.json"
    }]
  },
  "scripts": {
    "build": "tsc -p ."
  },
  "devDependencies": {
    "typescript": "^5.4.0",
    "vscode": "^1.1.37"
  }
}
'@ | Out-File -Encoding utf8 src/vscode-mplx/package.json -NoNewline

@'
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ES2020",
    "outDir": "dist",
    "strict": true,
    "moduleResolution": "bundler",
    "esModuleInterop": true,
    "skipLibCheck": true
  },
  "include": ["src"]
}
'@ | Out-File -Encoding utf8 src/vscode-mplx/tsconfig.json -NoNewline

@'
import * as vscode from 'vscode'

export function activate(_context: vscode.ExtensionContext) {
  console.log('MPLX extension active')
}

export function deactivate() {}
'@ | Out-File -Encoding utf8 src/vscode-mplx/src/extension.ts -NoNewline

@'
{
  "comments": {
    "lineComment": "//",
    "blockComment": ["/*", "*/"]
  },
  "brackets": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"]
  ]
}
'@ | Out-File -Encoding utf8 src/vscode-mplx/language-configuration.json -NoNewline

# Done
Write-Host "MPLX bootstrap files generated."

