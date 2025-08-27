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