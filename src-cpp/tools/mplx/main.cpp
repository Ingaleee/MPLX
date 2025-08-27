#include "../../mplx-lang/lexer.hpp"
#include "../../mplx-lang/parser.hpp"
#include "../../mplx-compiler/compiler.hpp"
#include "../../mplx-vm/vm.hpp"
#include <fmt/core.h>
#include <fstream>
#include <sstream>

int main(int argc, char** argv){
  std::string src;
  if(argc>=3 && std::string(argv[1])=="--run"){
    std::ifstream ifs(argv[2]);
    std::stringstream ss; ss << ifs.rdbuf(); src = ss.str();
  } else {
    src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  }

  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();
  if(!ps.diagnostics().empty()){
    for(auto& d : ps.diagnostics()) fmt::print("parser: {}\n", d);
    return 1;
  }
  mplx::Compiler c;
  auto res = c.compile(mod);
  if(!res.diags.empty()){
    for(auto& d : res.diags) fmt::print("compile: {}\n", d);
    return 1;
  }
  mplx::VM vm(res.bc);
  auto v = vm.run("main");
  fmt::print("{}\n", v);
  return 0;
}