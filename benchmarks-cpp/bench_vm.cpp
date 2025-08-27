#include <benchmark/benchmark.h>
#include "../src-cpp/mplx-lang/lexer.hpp"
#include "../src-cpp/mplx-lang/parser.hpp"
#include "../src-cpp/mplx-compiler/compiler.hpp"
#include "../src-cpp/mplx-vm/vm.hpp"

static void BM_CompileAndRun(benchmark::State& state) {
  const char* src = "fn main() -> i32 { let x = 0; x = x + 1 * 2; return x; }";
  for (auto _ : state) {
    mplx::Lexer lx(src);
    auto toks = lx.Lex();
    mplx::Parser ps(std::move(toks));
    auto mod = ps.parse();
    mplx::Compiler c;
    auto res = c.compile(mod);
    mplx::VM vm(res.bc);
    auto v = vm.run("main");
    benchmark::DoNotOptimize(v);
  }
}

BENCHMARK(BM_CompileAndRun);

static void BM_RunOnly(benchmark::State& state) {
  const char* src = "fn main() -> i32 { let x = 0; x = x + 1 * 2; return x; }";
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();
  mplx::Compiler c;
  auto res = c.compile(mod);
  mplx::VM vm(res.bc);
  for (auto _ : state) {
    auto v = vm.run("main");
    benchmark::DoNotOptimize(v);
  }
}

BENCHMARK(BM_RunOnly);

static void BM_CompileOnly(benchmark::State& state) {
  const char* src = "fn main() -> i32 { let x = 0; x = x + 1 * 2; return x; }";
  for (auto _ : state) {
    mplx::Lexer lx(src);
    auto toks = lx.Lex();
    mplx::Parser ps(std::move(toks));
    auto mod = ps.parse();
    mplx::Compiler c;
    auto res = c.compile(mod);
    benchmark::DoNotOptimize(res.bc.consts.size());
  }
}

BENCHMARK(BM_CompileOnly);

BENCHMARK_MAIN();

