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
      {"while", TokenKind::KwWhile},
  };

  void Lexer::skip_ws() {
    for (;;) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r') {
        get();
        ++col_;
      } else if (c == '\n') {
        get();
        ++line_;
        col_ = 1;
      } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
        while (peek() != '\n' && peek() != '\0') {
          get();
        }
      } else {
        break;
      }
    }
  }

  Token Lexer::make(TokenKind k, std::size_t start, std::size_t line, std::size_t col, std::size_t len) {
    Token t;
    t.kind   = k;
    t.lexeme = src_.substr(start, len);
    t.line   = line;
    t.col    = col;
    return t;
  }

  std::vector<Token> Lexer::Lex() {
    std::vector<Token> out;
    while (true) {
      skip_ws();
      std::size_t tok_line = line_, tok_col = col_;
      char c = peek();
      if (c == '\0') {
        out.push_back(Token{TokenKind::Eof, "", tok_line, tok_col});
        break;
      }

      if (std::isalpha((unsigned char)c) || c == '_') {
        std::size_t start = pos_;
        std::size_t col0  = col_;
        while (std::isalnum((unsigned char)peek()) || peek() == '_') {
          get();
          ++col_;
        }
        std::string s = src_.substr(start, pos_ - start);
        auto it       = kKeywords.find(s);
        out.push_back(make(it == kKeywords.end() ? TokenKind::Identifier : it->second, start, tok_line, col0, pos_ - start));
        continue;
      }

      if (std::isdigit((unsigned char)c)) {
        std::size_t start = pos_;
        std::size_t col0  = col_;
        while (std::isdigit((unsigned char)peek())) {
          get();
          ++col_;
        }
        out.push_back(make(TokenKind::Number, start, tok_line, col0, pos_ - start));
        continue;
      }

      // two-char tokens
      if (c == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '>') {
        get();
        get();
        out.push_back(Token{TokenKind::Arrow, "->", tok_line, tok_col});
        col_ += 2;
        continue;
      }
      if (c == '=' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        out.push_back(Token{TokenKind::EqEq, "==", tok_line, tok_col});
        col_ += 2;
        continue;
      }
      if (c == '!' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        out.push_back(Token{TokenKind::BangEq, "!=", tok_line, tok_col});
        col_ += 2;
        continue;
      }
      if (c == '<' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        out.push_back(Token{TokenKind::Le, "<=", tok_line, tok_col});
        col_ += 2;
        continue;
      }
      if (c == '>' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '=') {
        get();
        get();
        out.push_back(Token{TokenKind::Ge, ">=", tok_line, tok_col});
        col_ += 2;
        continue;
      }

      // single-char
      get();
      switch (c) {
      case '(':
        out.push_back(Token{TokenKind::LParen, "(", tok_line, tok_col});
        ++col_;
        break;
      case ')':
        out.push_back(Token{TokenKind::RParen, ")", tok_line, tok_col});
        ++col_;
        break;
      case '{':
        out.push_back(Token{TokenKind::LBrace, "{", tok_line, tok_col});
        ++col_;
        break;
      case '}':
        out.push_back(Token{TokenKind::RBrace, "}", tok_line, tok_col});
        ++col_;
        break;
      case ',':
        out.push_back(Token{TokenKind::Comma, ",", tok_line, tok_col});
        ++col_;
        break;
      case ';':
        out.push_back(Token{TokenKind::Semicolon, ";", tok_line, tok_col});
        ++col_;
        break;
      case ':':
        out.push_back(Token{TokenKind::Colon, ":", tok_line, tok_col});
        ++col_;
        break;
      case '+':
        out.push_back(Token{TokenKind::Plus, "+", tok_line, tok_col});
        ++col_;
        break;
      case '-':
        out.push_back(Token{TokenKind::Minus, "-", tok_line, tok_col});
        ++col_;
        break;
      case '*':
        out.push_back(Token{TokenKind::Star, "*", tok_line, tok_col});
        ++col_;
        break;
      case '/':
        out.push_back(Token{TokenKind::Slash, "/", tok_line, tok_col});
        ++col_;
        break;
      case '=':
        out.push_back(Token{TokenKind::Equal, "=", tok_line, tok_col});
        ++col_;
        break;
      case '<':
        out.push_back(Token{TokenKind::Lt, "<", tok_line, tok_col});
        ++col_;
        break;
      case '>':
        out.push_back(Token{TokenKind::Gt, ">", tok_line, tok_col});
        ++col_;
        break;
      default:
        ++col_;
        break;
      }
    }
    return out;
  }

} // namespace mplx