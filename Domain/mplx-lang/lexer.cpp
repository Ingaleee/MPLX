#include "lexer.hpp"
#include <unordered_map>

namespace mplx {
    namespace {
        bool ascii_alpha(char value) {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        }
        bool ascii_digit(char value) {
            return value >= '0' && value <= '9';
        }
    } // namespace
    void Lexer::skip_ws() {
        for (;;) {
            const auto c = peek();
            if (c == ' ' || c == '\t' || c == '\r') {
                get();
                ++col_;
            } else if (c == '\n') {
                get();
                ++line_;
                col_ = 1;
            } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
                while (pos_ < src_.size() && peek() != '\n') {
                    get();
                    ++col_;
                }
            } else
                break;
        }
    }
    Token Lexer::make(TokenKind kind, size_t start, size_t line, size_t col, size_t length) {
        return {kind, src_.substr(start, length), line, col, static_cast<uint32_t>(start)};
    }
    std::vector<Token> Lexer::Lex() {
        static const std::unordered_map<std::string, TokenKind> keywords = {
            {"import", TokenKind::KwImport}, {"as", TokenKind::KwAs},      {"export", TokenKind::KwExport},
            {"fn", TokenKind::KwFn},         {"let", TokenKind::KwLet},    {"return", TokenKind::KwReturn},
            {"if", TokenKind::KwIf},         {"else", TokenKind::KwElse},  {"while", TokenKind::KwWhile},
            {"true", TokenKind::KwTrue},     {"false", TokenKind::KwFalse}};
        std::vector<Token> tokens;
        while (true) {
            skip_ws();
            const auto start = pos_, line = line_, col = col_;
            if (pos_ == src_.size()) {
                tokens.push_back(make(TokenKind::Eof, start, line, col, 0));
                break;
            }
            auto c = peek();
            TokenKind kind = TokenKind::Invalid;
            if (ascii_alpha(c) || c == '_') {
                do {
                    get();
                    ++col_;
                } while (ascii_alpha(peek()) || ascii_digit(peek()) || peek() == '_');
                auto it = keywords.find(src_.substr(start, pos_ - start));
                kind = it == keywords.end() ? TokenKind::Identifier : it->second;
            } else if (ascii_digit(c)) {
                do {
                    get();
                    ++col_;
                } while (ascii_digit(peek()));
                kind = TokenKind::Number;
            } else if (c == '"') {
                get();
                ++col_;
                bool valid = true;
                while (pos_ < src_.size() && peek() != '"' && peek() != '\n' && peek() != '\r') {
                    if (peek() == '\\' || peek() == '\0') {
                        valid = false;
                    }
                    get();
                    ++col_;
                }
                if (pos_ < src_.size() && peek() == '"') {
                    get();
                    ++col_;
                    kind = valid ? TokenKind::ImportPath : TokenKind::Invalid;
                }
            } else {
                const auto n = pos_ + 1 < src_.size() ? src_[pos_ + 1] : '\0';
                if (c == ':' && n == ':')
                    kind = TokenKind::DoubleColon;
                else if (c == '-' && n == '>')
                    kind = TokenKind::Arrow;
                else if (c == '=' && n == '=')
                    kind = TokenKind::EqEq;
                else if (c == '!' && n == '=')
                    kind = TokenKind::BangEq;
                else if (c == '<' && n == '=')
                    kind = TokenKind::Le;
                else if (c == '>' && n == '=')
                    kind = TokenKind::Ge;
                else if (c == '&' && n == '&')
                    kind = TokenKind::AndAnd;
                else if (c == '|' && n == '|')
                    kind = TokenKind::OrOr;
                if (kind != TokenKind::Invalid) {
                    get();
                    get();
                    col_ += 2;
                } else {
                    get();
                    ++col_;
                    switch (c) {
                    case '(':
                        kind = TokenKind::LParen;
                        break;
                    case ')':
                        kind = TokenKind::RParen;
                        break;
                    case '{':
                        kind = TokenKind::LBrace;
                        break;
                    case '}':
                        kind = TokenKind::RBrace;
                        break;
                    case ',':
                        kind = TokenKind::Comma;
                        break;
                    case ';':
                        kind = TokenKind::Semicolon;
                        break;
                    case ':':
                        kind = TokenKind::Colon;
                        break;
                    case '+':
                        kind = TokenKind::Plus;
                        break;
                    case '-':
                        kind = TokenKind::Minus;
                        break;
                    case '*':
                        kind = TokenKind::Star;
                        break;
                    case '/':
                        kind = TokenKind::Slash;
                        break;
                    case '%':
                        kind = TokenKind::Percent;
                        break;
                    case '!':
                        kind = TokenKind::Bang;
                        break;
                    case '=':
                        kind = TokenKind::Equal;
                        break;
                    case '<':
                        kind = TokenKind::Lt;
                        break;
                    case '>':
                        kind = TokenKind::Gt;
                        break;
                    default:
                        break;
                    }
                }
            }
            tokens.push_back(make(kind, start, line, col, pos_ - start));
        }
        return tokens;
    }
} // namespace mplx
