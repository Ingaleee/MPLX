#include "parser.hpp"
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace mplx {
    namespace {
        struct DepthGuard {
            size_t &depth;
            explicit DepthGuard(size_t &d) : depth(d) {
                if (d >= 128)
                    throw std::runtime_error("syntax nesting exceeds 128 levels");
                ++d;
            }
            ~DepthGuard() {
                --depth;
            }
        };
        SourceSpan span(const Token &token) {
            auto length = static_cast<uint32_t>(token.lexeme.size());
            return {{token.offset, static_cast<uint32_t>(token.line), static_cast<uint32_t>(token.col)},
                    {token.offset + length, static_cast<uint32_t>(token.line),
                     static_cast<uint32_t>(token.col) + length}};
        }
        std::unique_ptr<Expr> binary(std::unique_ptr<Expr> lhs, const Token &op, std::unique_ptr<Expr> rhs) {
            auto range = join(lhs->span, rhs->span);
            auto node = std::make_unique<BinaryExpr>(std::move(lhs), op.lexeme, std::move(rhs));
            node->span = range;
            node->operator_span = span(op);
            return node;
        }
    } // namespace
    const Token &Parser::peek() const {
        return t[i];
    }
    const Token &Parser::prev() const {
        return t[i ? i - 1 : 0];
    }
    bool Parser::is_at_end() const {
        return peek().kind == TokenKind::Eof;
    }
    const Token &Parser::advance() {
        const auto &value = peek();
        if (!is_at_end())
            ++i;
        return value;
    }
    bool Parser::check(TokenKind k) const {
        return peek().kind == k;
    }
    bool Parser::match(TokenKind k) {
        if (!check(k))
            return false;
        advance();
        return true;
    }
    void Parser::expect(TokenKind k, const char *message) {
        if (!match(k))
            error_here(message);
    }
    void Parser::error_here(const std::string &text) {
        auto range = span(peek());
        std::ostringstream out;
        out << "[line " << peek().line << ":" << peek().col << "] " << text;
        diags.push_back(out.str());
        structured_.push_back({"MPLX1001", out.str(), range.start.line, range.start.column, range});
    }
    Module Parser::parse() {
        Module module;
        while (!is_at_end()) {
            const auto before = i;
            if (check(TokenKind::KwImport)) {
                module.imports.push_back(parseImport());
            } else {
                module.functions.push_back(parseFunction());
            }
            if (i == before) {
                error_here("parser made no progress");
                advance();
            }
        }
        return module;
    }
    std::vector<std::unique_ptr<Stmt>> Parser::block(SourceSpan &range) {
        range = span(peek());
        expect(TokenKind::LBrace, "expected '{'");
        std::vector<std::unique_ptr<Stmt>> body;
        while (!check(TokenKind::RBrace) && !is_at_end()) {
            const auto before = i;
            if (auto statement = parseStmt())
                body.push_back(std::move(statement));
            if (before == i) {
                error_here("parser made no progress in block");
                advance();
            }
        }
        range = join(range, span(peek()));
        expect(TokenKind::RBrace, "expected '}'");
        return body;
    }
    Module::Import Parser::parseImport() {
        Module::Import value;
        const auto start = span(advance());
        if (check(TokenKind::ImportPath)) {
            const auto token = advance();
            value.path = token.lexeme.substr(1, token.lexeme.size() - 2);
            value.path_span = span(token);
        } else {
            error_here("expected quoted import path (use '/' separators, without escapes)");
            if (!is_at_end()) {
                advance();
            }
        }
        expect(TokenKind::KwAs, "expected 'as' after import path");
        if (check(TokenKind::Identifier)) {
            const auto token = advance();
            value.alias = token.lexeme;
            value.alias_span = span(token);
        } else {
            error_here("expected import namespace identifier");
        }
        value.span = join(start, span(peek()));
        expect(TokenKind::Semicolon, "expected ';' after import");
        return value;
    }
    Function Parser::parseFunction() {
        const auto start = span(peek());
        const bool exported = match(TokenKind::KwExport);
        expect(TokenKind::KwFn, "expected 'fn'");
        if (!check(TokenKind::Identifier))
            error_here("expected function name");
        auto name = advance();
        expect(TokenKind::LParen, "expected '('");
        auto params = parseParams();
        expect(TokenKind::RParen, "expected ')'");
        std::string return_type = "i32";
        if (match(TokenKind::Arrow)) {
            if (check(TokenKind::Identifier))
                return_type = advance().lexeme;
            else
                error_here("expected return type");
        }
        Function fn;
        fn.exported = exported;
        fn.name = name.lexeme;
        fn.name_span = span(name);
        fn.params = std::move(params);
        fn.returnType = return_type;
        fn.body = block(fn.body_span);
        fn.span = join(start, fn.body_span);
        return fn;
    }
    std::vector<Param> Parser::parseParams() {
        std::vector<Param> params;
        if (check(TokenKind::RParen))
            return params;
        do {
            if (!check(TokenKind::Identifier)) {
                error_here("expected param name");
                break;
            }
            auto name = advance();
            std::string type = "i32";
            if (match(TokenKind::Colon)) {
                if (check(TokenKind::Identifier))
                    type = advance().lexeme;
                else
                    error_here("expected parameter type");
            }
            params.push_back({name.lexeme, type, span(name)});
        } while (match(TokenKind::Comma));
        return params;
    }
    std::unique_ptr<Stmt> Parser::parseStmt() {
        DepthGuard guard(depth_);
        if (check(TokenKind::KwIf))
            return parseIf();
        if (check(TokenKind::KwWhile))
            return parseWhile();
        if (check(TokenKind::KwLet))
            return parseLet();
        if (check(TokenKind::KwReturn))
            return parseReturn();
        return parseExprStmt();
    }
    std::unique_ptr<Stmt> Parser::parseIf() {
        auto start = span(advance());
        expect(TokenKind::LParen, "expected '(' after if");
        auto node = std::make_unique<IfStmt>();
        node->cond = expression();
        expect(TokenKind::RParen, "expected ')' after condition");
        node->thenS = block(node->then_span);
        if (match(TokenKind::KwElse))
            node->elseS = block(node->else_span);
        node->span = join(start, node->elseS.empty() ? node->then_span : node->else_span);
        return node;
    }
    std::unique_ptr<Stmt> Parser::parseWhile() {
        auto start = span(advance());
        expect(TokenKind::LParen, "expected '(' after while");
        auto condition = expression();
        expect(TokenKind::RParen, "expected ')' after while condition");
        SourceSpan body_span;
        std::vector<std::unique_ptr<Stmt>> body;
        if (check(TokenKind::LBrace))
            body = block(body_span);
        else {
            auto statement = parseStmt();
            if (statement) {
                body_span = statement->span;
                body.push_back(std::move(statement));
            }
        }
        auto node = std::make_unique<WhileStmt>(std::move(condition), std::move(body));
        node->body_span = body_span;
        node->span = join(start, body_span);
        return node;
    }
    std::unique_ptr<Stmt> Parser::parseLet() {
        auto start = span(advance());
        if (!check(TokenKind::Identifier)) {
            error_here("expected identifier after let");
            return nullptr;
        }
        auto name = advance();
        std::string type;
        if (match(TokenKind::Colon)) {
            if (check(TokenKind::Identifier))
                type = advance().lexeme;
            else
                error_here("expected local type");
        }
        expect(TokenKind::Equal, "expected '=' after let name");
        auto init = expression();
        auto end = span(peek());
        expect(TokenKind::Semicolon, "expected ';' after let");
        auto node = std::make_unique<LetStmt>(name.lexeme, std::move(init));
        node->typeName = type;
        node->name_span = span(name);
        node->span = join(start, end);
        return node;
    }
    std::unique_ptr<Stmt> Parser::parseReturn() {
        auto start = span(advance());
        auto value = expression();
        auto end = span(peek());
        expect(TokenKind::Semicolon, "expected ';' after return");
        auto node = std::make_unique<ReturnStmt>(std::move(value));
        node->span = join(start, end);
        return node;
    }
    std::unique_ptr<Stmt> Parser::parseExprStmt() {
        auto start = span(peek());
        if (check(TokenKind::Identifier) && i + 1 < t.size() && t[i + 1].kind == TokenKind::Equal) {
            auto name = advance();
            advance();
            auto value = expression();
            auto end = span(peek());
            expect(TokenKind::Semicolon, "expected ';' after assignment");
            auto node = std::make_unique<AssignStmt>(name.lexeme, std::move(value));
            node->name_span = span(name);
            node->span = join(start, end);
            return node;
        }
        auto value = expression();
        auto end = span(peek());
        expect(TokenKind::Semicolon, "expected ';' after expression");
        auto node = std::make_unique<ExprStmt>(std::move(value));
        node->span = join(start, end);
        return node;
    }
    std::unique_ptr<Expr> Parser::expression() {
        DepthGuard guard(depth_);
        return logicalOr();
    }
    std::unique_ptr<Expr> Parser::logicalOr() {
        auto e = logicalAnd();
        while (check(TokenKind::OrOr)) {
            auto op = advance();
            auto rhs = logicalAnd();
            e = binary(std::move(e), op, std::move(rhs));
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::logicalAnd() {
        auto e = equality();
        while (check(TokenKind::AndAnd)) {
            auto op = advance();
            auto rhs = equality();
            e = binary(std::move(e), op, std::move(rhs));
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::equality() {
        auto e = comparison();
        while (check(TokenKind::EqEq) || check(TokenKind::BangEq)) {
            auto op = advance();
            auto rhs = comparison();
            e = binary(std::move(e), op, std::move(rhs));
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::comparison() {
        auto e = term();
        while (check(TokenKind::Lt) || check(TokenKind::Le) || check(TokenKind::Gt) || check(TokenKind::Ge)) {
            auto op = advance();
            auto rhs = term();
            e = binary(std::move(e), op, std::move(rhs));
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::term() {
        auto e = factor();
        while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
            auto op = advance();
            auto rhs = factor();
            e = binary(std::move(e), op, std::move(rhs));
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::factor() {
        auto e = unary();
        while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
            auto op = advance();
            auto rhs = unary();
            e = binary(std::move(e), op, std::move(rhs));
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::unary() {
        DepthGuard guard(depth_);
        if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
            auto op = advance();
            auto rhs = unary();
            auto range = join(span(op), rhs->span);
            auto node = std::make_unique<UnaryExpr>(op.lexeme, std::move(rhs));
            node->span = range;
            node->operator_span = span(op);
            return node;
        }
        return call();
    }
    std::unique_ptr<Expr> Parser::call() {
        auto e = primary();
        while (match(TokenKind::LParen)) {
            auto callee = dynamic_cast<VarExpr *>(e.get());
            if (!callee) {
                error_here("expression is not callable");
                return e;
            }
            auto node = std::make_unique<CallExpr>(callee->name);
            node->name_span = callee->name_span;
            if (!check(TokenKind::RParen)) {
                do {
                    node->args.push_back(expression());
                    node->checkDepth(std::max(node->depth, 1 + node->args.back()->depth));
                } while (match(TokenKind::Comma));
            }
            node->span = join(e->span, span(peek()));
            expect(TokenKind::RParen, "expected ')' after call");
            e = std::move(node);
        }
        return e;
    }
    std::unique_ptr<Expr> Parser::primary() {
        auto token = peek();
        if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse)) {
            auto node = std::make_unique<LiteralExpr>(token.kind == TokenKind::KwTrue ? 1 : 0);
            node->is_bool = true;
            node->span = span(token);
            return node;
        }
        if (check(TokenKind::Number)) {
            long long value = 0;
            auto parsed =
                std::from_chars(token.lexeme.data(), token.lexeme.data() + token.lexeme.size(), value);
            if (parsed.ec != std::errc{})
                error_here("integer literal out of range");
            advance();
            auto node = std::make_unique<LiteralExpr>(value);
            node->span = span(token);
            return node;
        }
        if (match(TokenKind::Identifier)) {
            auto node = std::make_unique<VarExpr>(token.lexeme);
            node->span = span(token);
            node->name_span = span(token);
            if (match(TokenKind::DoubleColon)) {
                if (!check(TokenKind::Identifier)) {
                    error_here("expected function name after '::'");
                } else {
                    const auto member = advance();
                    node->name += "::" + member.lexeme;
                    node->name_span = span(member);
                    node->span = join(node->span, span(member));
                }
            }
            return node;
        }
        if (match(TokenKind::LParen)) {
            auto e = expression();
            expect(TokenKind::RParen, "expected ')'");
            return e;
        }
        error_here("unexpected token in expression");
        if (!is_at_end())
            advance();
        auto node = std::make_unique<LiteralExpr>(0);
        node->span = span(token);
        return node;
    }
} // namespace mplx
