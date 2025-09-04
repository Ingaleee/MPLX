#include <gtest/gtest.h>
#include "../../Domain/mplx-lang/lexer.hpp"
#include "../../Domain/mplx-lang/parser.hpp"
#include "../../Application/mplx-compiler/compiler.hpp"
#include "../../Application/mplx-vm/vm.hpp"

static long long run_src(const char* src){
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto m = ps.parse();
  mplx::Compiler c; auto res = c.compile(m);
  EXPECT_TRUE(ps.diagnostics().empty());
  EXPECT_TRUE(res.diags.empty());
  mplx::VM vm(res.bc);
  return vm.run("main");
}

TEST(Optimizer, AlgebraicSimplify){
  EXPECT_EQ(run_src("fn main()->i32{ let x = 21+0; return x*2; }"), 42);
  EXPECT_EQ(run_src("fn main()->i32{ let x = 21*1; return x*2; }"), 42);
  EXPECT_EQ(run_src("fn main()->i32{ let x = 0*99; return x+42; }"), 42);
  EXPECT_EQ(run_src("fn main()->i32{ let x = 42/1; return x; }"), 42);
}

TEST(Optimizer, ConstIfFolding){
  EXPECT_EQ(run_src("fn main()->i32{ if(true){ return 42; } else { return 0; } }"), 42);
  EXPECT_EQ(run_src("fn main()->i32{ if(false){ return 0; } else { return 42; } }"), 42);
}













