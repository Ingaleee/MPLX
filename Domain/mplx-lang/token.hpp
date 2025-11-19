#pragma once
#include <cstdint>
#include <string>

namespace mplx {

    enum class TokenKind {
        Eof,
        Invalid,
        KwTrue,
        KwFalse,
        Identifier,
        Number,
        ImportPath,
        KwImport,
        KwAs,
        KwExport,
        KwFn,
        KwLet,
        KwReturn,
        KwIf,
        KwElse,
        KwWhile,
        LParen,
        RParen,
        LBrace,
        RBrace,
        Comma,
        Semicolon,
        Colon,
        DoubleColon,
        Arrow, // ->
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Bang,
        AndAnd,
        OrOr,
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
        uint32_t offset{0};
    };

} // namespace mplx
