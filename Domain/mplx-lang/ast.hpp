#pragma once
#include "source.hpp"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mplx {

    struct Expr {
        SourceSpan span;
        virtual ~Expr() = default;
        size_t depth{1};
        void checkDepth(size_t value) {
            if (value > 128)
                throw std::runtime_error("expression nesting exceeds 128 levels");
            depth = value;
        }
    };

    struct LiteralExpr : Expr {
        long long value;
        bool is_bool{false};
        explicit LiteralExpr(long long v) : value(v) {
        }
    };
    struct VarExpr : Expr {
        SourceSpan name_span;
        std::string name;
        explicit VarExpr(std::string n) : name(std::move(n)) {
        }
    };
    struct UnaryExpr : Expr {
        SourceSpan operator_span;
        std::string op;
        std::unique_ptr<Expr> rhs;
        UnaryExpr(std::string o, std::unique_ptr<Expr> r) : op(std::move(o)), rhs(std::move(r)) {
            checkDepth(1 + rhs->depth);
        }
    };
    struct BinaryExpr : Expr {
        SourceSpan operator_span;
        std::string op;
        std::unique_ptr<Expr> lhs, rhs;
        BinaryExpr(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
            : op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {
            checkDepth(1 + std::max(lhs->depth, rhs->depth));
        }
    };
    struct CallExpr : Expr {
        SourceSpan name_span;
        std::string callee;
        std::vector<std::unique_ptr<Expr>> args;
        explicit CallExpr(std::string c) : callee(std::move(c)) {
        }
    };

    struct Stmt {
        SourceSpan span;
        virtual ~Stmt() = default;
    };
    struct LetStmt : Stmt {
        SourceSpan name_span;
        std::string typeName;
        std::string name;
        std::unique_ptr<Expr> init;
        LetStmt(std::string n, std::unique_ptr<Expr> i) : name(std::move(n)), init(std::move(i)) {
        }
    };
    struct AssignStmt : Stmt {
        SourceSpan name_span;
        std::string name;
        std::unique_ptr<Expr> value;
        AssignStmt(std::string n, std::unique_ptr<Expr> v) : name(std::move(n)), value(std::move(v)) {
        }
    };
    struct ReturnStmt : Stmt {
        std::unique_ptr<Expr> value;
        explicit ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {
        }
    };
    struct ExprStmt : Stmt {
        std::unique_ptr<Expr> expr;
        explicit ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {
        }
    };
    struct IfStmt : Stmt {
        SourceSpan then_span, else_span;
        std::unique_ptr<Expr> cond;
        std::vector<std::unique_ptr<Stmt>> thenS;
        std::vector<std::unique_ptr<Stmt>> elseS;
    };

    struct WhileStmt : Stmt {
        SourceSpan body_span;
        std::unique_ptr<Expr> cond;
        std::vector<std::unique_ptr<Stmt>> body;
        WhileStmt(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Stmt>> b)
            : cond(std::move(c)), body(std::move(b)) {
        }
    };

    struct Param {
        std::string name;
        std::string typeName;
        SourceSpan name_span;
    };

    struct Function {
        bool exported{false};
        SourceSpan span, name_span, body_span;
        std::string name;
        std::vector<Param> params;
        std::string returnType;
        std::vector<std::unique_ptr<Stmt>> body;
    };

    struct Module {
        struct Import {
            std::string path;
            std::string alias;
            SourceSpan span, path_span, alias_span;
        };
        std::vector<Import> imports;
        std::vector<Function> functions;
    };

} // namespace mplx
