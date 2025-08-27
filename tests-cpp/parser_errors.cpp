#include <gtest/gtest.h>
#include "../src-cpp/mplx-lang/lexer.hpp"
#include "../src-cpp/mplx-lang/parser.hpp"

TEST(Parser, ReportsErrorOnMissingSemicolon){
  const char* src = "fn main() -> i32 { let x = 1 + 2 return x; }"; // missing ';' before return
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto m = ps.parse(); (void)m;
  ASSERT_FALSE(ps.diagnostics().empty());
}


