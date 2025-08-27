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