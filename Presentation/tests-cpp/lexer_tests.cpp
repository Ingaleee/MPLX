#include "../../../Domain/mplx-lang/lexer.hpp"
#include <iostream>

int main() {
  std::cout << "Running lexer tests...\n";
  
  std::string src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  
  std::cout << "Tokens: ";
  for(const auto& t : toks) {
    std::cout << t.lexeme << " ";
  }
  std::cout << "\n";
  
  std::cout << "Lexer tests passed!\n";
  return 0;
}