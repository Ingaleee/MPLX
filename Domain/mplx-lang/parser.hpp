#pragma once
#include "ast.hpp"
#include "diagnostic.hpp"
#include "token.hpp"
#include <vector>

namespace mplx {
    class Parser {
      public:
        explicit Parser(std::vector<Token> tokens) : t(std::move(tokens)) {
            if (t.empty() || t.back().kind != TokenKind::Eof)
                t.push_back(Token{TokenKind::Eof, "", 1, 1});
        }
        Module parse();
        const std::vector<std::string> &diagnostics() const {
            return diags;
        }
        const std::vector<Diagnostic> &structuredDiagnostics() const {
            return structured_;
        }

      private:
        bool match(TokenKind kind);
        bool check(TokenKind kind) const;
        const Token &advance();
        const Token &peek() const;
        const Token &prev() const;
        bool is_at_end() const;
        void error_here(const std::string &message);
        void expect(TokenKind kind, const char *message);
        Function parseFunction();
        Module::Import parseImport();
        std::vector<Param> parseParams();
        std::vector<std::unique_ptr<Stmt>> block(SourceSpan &span);
        std::unique_ptr<Stmt> parseStmt();
        std::unique_ptr<Stmt> parseIf();
        std::unique_ptr<Stmt> parseWhile();
        std::unique_ptr<Stmt> parseLet();
        std::unique_ptr<Stmt> parseReturn();
        std::unique_ptr<Stmt> parseExprStmt();
        std::unique_ptr<Expr> expression();
        std::unique_ptr<Expr> logicalOr();
        std::unique_ptr<Expr> logicalAnd();
        std::unique_ptr<Expr> equality();
        std::unique_ptr<Expr> comparison();
        std::unique_ptr<Expr> term();
        std::unique_ptr<Expr> factor();
        std::unique_ptr<Expr> unary();
        std::unique_ptr<Expr> call();
        std::unique_ptr<Expr> primary();
        std::vector<Token> t;
        size_t i{0}, depth_{0};
        std::vector<std::string> diags;
        std::vector<Diagnostic> structured_;
    };
} // namespace mplx
