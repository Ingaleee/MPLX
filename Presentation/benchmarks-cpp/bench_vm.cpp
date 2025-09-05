#include "../../../Application/mplx-compiler/compiler.hpp"
#include "../../../Application/mplx-vm/vm.hpp"
#include "../../../Domain/mplx-lang/lexer.hpp"
#include "../../../Domain/mplx-lang/parser.hpp"
#include <chrono>
#include <iostream>

static void BM_CompileAndRun() {
  const char *src = "fn main() -> i32 { let x = 0; x = x + 1 * 2; return x; }";
  auto start      = std::chrono::high_resolution_clock::now();

  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();
  mplx::Compiler c;
  auto res = c.compile(mod);
  mplx::VM vm(res.bc);
  auto v = vm.run("main");

  auto end      = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "CompileAndRun: " << duration.count() << " microseconds, result: " << v << std::endl;
}

static void BM_RunOnly() {
  const char *src = "fn main() -> i32 { let x = 0; x = x + 1 * 2; return x; }";
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();
  mplx::Compiler c;
  auto res = c.compile(mod);
  mplx::VM vm(res.bc);

  auto start    = std::chrono::high_resolution_clock::now();
  auto v        = vm.run("main");
  auto end      = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "RunOnly: " << duration.count() << " microseconds, result: " << v << std::endl;
}

int main() {
  std::cout << "MPLX Benchmarks (simplified version)\n";
  std::cout << "Note: Full benchmarks require Google Benchmark library\n\n";

  BM_CompileAndRun();
  BM_RunOnly();

  return 0;
}
