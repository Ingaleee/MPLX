#pragma once
#include <cstdint>
#include <string>

namespace mplx {

  enum class TokenKind {
    Eof,
    Identifier,
    Number,
    KwFn,
    KwLet,
    KwReturn,
    KwIf,
    KwElse,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semicolon,
    Colon,
    Arrow, // ->
    Plus,
    Minus,
    Star,
    Slash,
    Equal, // =
    EqEq,
    BangEq,
    Lt,
    Le,
    Gt,
    Ge,
  };

  struct Token {
    TokenKind kind{};
    std::string lexeme{};
    std::size_t line{1};
    std::size_t col{1};
  };

} // namespace mplx