#include "../../../Domain/mplx-lang/lexer.hpp"
#include "../../../Domain/mplx-lang/parser.hpp"
#include "../../../Application/mplx-compiler/compiler.hpp"
#include "../../../Application/mplx-vm/vm.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv){
  std::string mode;
  std::string fileArg;
  std::string src;

  auto print_usage = [](){
    std::cout << "Usage: mplx [--run|--check|--symbols] <file>\n";
  };

  if(argc >= 2) mode = argv[1];
  if(mode == "--help" || argc < 2){ print_usage(); return 0; }
  if(mode == "--version"){ std::cout << "mplx 0.2.0\n"; return 0; }
  if((mode == "--run" || mode == "--check" || mode == "--symbols") && argc >= 3) fileArg = argv[2];
  if((mode == "--run" || mode == "--check" || mode == "--symbols") && fileArg.empty()){ print_usage(); return 2; }
  if(!(mode == "--run" || mode == "--check" || mode == "--symbols")) { print_usage(); return 2; }

  if(mode == "--run" || mode == "--check" || mode == "--symbols"){
    std::ifstream ifs(fileArg);
    if(!ifs){
      if(mode == "--check"){
        std::cout << "{\"diagnostics\": [{\"message\": \"cannot open file: " << fileArg << "\", \"line\": 0, \"col\": 0}]}\n";
        return 1;
      }
      std::cout << "cannot open file: " << fileArg << "\n";
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
    std::cout << "{\"diagnostics\": [";
    bool first = true;
    for(const auto& d : ps.diagnostics()){
      if(!first) std::cout << ", ";
      std::cout << "{\"message\": \"" << d << "\", \"line\": 0, \"col\": 0}";
      first = false;
    }
    std::cout << "]}\n";
    return ps.diagnostics().empty() ? 0 : 1;
  }

  if(mode == "--symbols"){
    std::cout << "{\"functions\": [";
    bool first = true;
    for(const auto& f : mod.functions){
      if(!first) std::cout << ", ";
      std::cout << "{\"name\": \"" << f.name << "\", \"arity\": " << f.params.size() << "}";
      first = false;
    }
    std::cout << "]}\n";
    return 0;
  }

  if(mode == "--run"){
    try {
      mplx::Compiler c;
      auto res = c.compile(mod);
      if(!res.diags.empty()){
        std::cout << "Compilation errors:\n";
        for(const auto& d : res.diags) std::cout << d << "\n";
        return 1;
      }
      
      mplx::VM vm(res.bc);
      auto result = vm.run("main");
      std::cout << "Result: " << result << "\n";
      return 0;
    } catch(const std::exception& e) {
      std::cout << "Runtime error: " << e.what() << "\n";
      return 1;
    }
  }

  return 0;
}