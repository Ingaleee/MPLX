#include <gtest/gtest.h>
#include "../src-cpp/mplx-lang/lexer.hpp"

TEST(Lexer, Basic){
  mplx::Lexer lx("fn main() { let x = 1 + 2; }");
  auto toks = lx.Lex();
  ASSERT_GE(toks.size(), 5);
}