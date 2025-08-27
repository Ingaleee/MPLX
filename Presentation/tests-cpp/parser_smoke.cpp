#include <gtest/gtest.h>
#include "../src-cpp/mplx-lang/lexer.hpp"
#include "../src-cpp/mplx-lang/parser.hpp"

TEST(Parser, Smoke){
  const char* src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto m = ps.parse();
  ASSERT_TRUE(ps.diagnostics().empty());
  ASSERT_FALSE(m.functions.empty());
  EXPECT_EQ(m.functions[0].name, "main");
}


