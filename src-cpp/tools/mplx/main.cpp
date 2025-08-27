#include "../../mplx-lang/lexer.hpp"
#include "../../mplx-lang/parser.hpp"
#include "../../mplx-compiler/compiler.hpp"
#include "../../mplx-vm/vm.hpp"
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

int main(int argc, char** argv){
  using nlohmann::json;
  std::string mode;
  std::string fileArg;
  std::string src;

  if(argc >= 2) mode = argv[1];
  if((mode == "--run" || mode == "--check") && argc >= 3) fileArg = argv[2];

  if(mode == "--run" || mode == "--check"){
    std::ifstream ifs(fileArg);
    if(!ifs){
      if(mode == "--check"){
        json j; j["diagnostics"] = { fmt::format("cannot open file: {}", fileArg) };
        fmt::print("{}\n", j.dump());
        return 1;
      }
      fmt::print("cannot open file: {}\n", fileArg);
      return 1;
    }
    std::stringstream ss; ss << ifs.rdbuf(); src = ss.str();
  } else {
    src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  }

  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();

  if(mode == "--check"){
    json j; j["diagnostics"] = json::array();
    for(const auto& d : ps.diagnostics()) j["diagnostics"].push_back(d);
    // compile too, to catch codegen-level issues
    if(ps.diagnostics().empty()){
      mplx::Compiler c;
      auto res = c.compile(mod);
      for(const auto& d : res.diags) j["diagnostics"].push_back(d);
    }
    fmt::print("{}\n", j.dump());
    return j["diagnostics"].empty() ? 0 : 1;
  }

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