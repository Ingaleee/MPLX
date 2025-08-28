#include "../../../Domain/mplx-lang/lexer.hpp"
#include "../../../Domain/mplx-lang/parser.hpp"
#include "../../../Application/mplx-compiler/compiler.hpp"
#include "../../../Application/mplx-vm/vm.hpp"
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

int main(int argc, char** argv){
  using nlohmann::json;
  std::string mode;
  std::string fileArg;
  std::string src;

  auto print_usage = [](){
    fmt::print("Usage: mplx [--run|--check|--symbols|--dump] <file>\n");
  };

  if(argc >= 2) mode = argv[1];
  if(mode == "--help" || argc < 2){ print_usage(); return 0; }
  if(mode == "--version"){ fmt::print("mplx 0.1.1\n"); return 0; }
  if((mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--dump") && argc >= 3) fileArg = argv[2];
  if((mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--dump") && fileArg.empty()){ print_usage(); return 2; }
  if(!(mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--dump")) { print_usage(); return 2; }

  if(mode == "--run" || mode == "--check" || mode == "--symbols"){
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
    auto push_diag = [&](const std::string& msg){
      std::size_t l=0,c=0; // default 0-based for editor, but store 1-based
      // parse prefix like "[line 12:34] ..."
      if(msg.size()>8 && msg[0]=='['){
        // naive scan
        auto p = msg.find("line ");
        auto q = msg.find(":", p==std::string::npos?0:p+5);
        auto r = msg.find("]", q==std::string::npos?0:q+1);
        if(p!=std::string::npos && q!=std::string::npos){
          try{
            l = std::stoul(msg.substr(p+5, q-(p+5)));
            if(r!=std::string::npos) c = std::stoul(msg.substr(q+1, r-(q+1)));
          } catch(...){ l=0; c=0; }
        }
      }
      // strip bracketed prefix for clean message
      std::string clean = msg;
      if(!msg.empty() && msg[0]=='['){
        auto rb = msg.find(']');
        if(rb!=std::string::npos && rb+2<=msg.size()) clean = msg.substr(rb+2);
      }
      j["diagnostics"].push_back({ {"message", clean}, {"line", l}, {"col", c} });
    };
    for(const auto& d : ps.diagnostics()) push_diag(d);
    // compile too, to catch codegen-level issues
    if(ps.diagnostics().empty()){
      mplx::Compiler c;
      auto res = c.compile(mod);
      for(const auto& d : res.diags) push_diag(d);
    }
    fmt::print("{}\n", j.dump());
    return j["diagnostics"].empty() ? 0 : 1;
  }

  if(mode == "--symbols"){
    json out; out["functions"] = json::array();
    for(const auto& f : mod.functions){
      out["functions"].push_back({ {"name", f.name}, {"arity", (int)f.params.size()} });
    }
    fmt::print("{}\n", out.dump());
    return 0;
  }

  if(mode == "--dump"){
    // print AST and bytecode (JSON)
    json out; out["ast"] = json::object(); out["ast"]["functions"] = json::array();
    for(const auto& f : mod.functions){
      json fn; fn["name"] = f.name; fn["params"] = json::array();
      for(const auto& p : f.params){ fn["params"].push_back(p.name); }
      out["ast"]["functions"].push_back(fn);
    }
    mplx::Compiler c; auto res = c.compile(mod);
    out["bytecode"] = nlohmann::json::parse(mplx::dump_bytecode_json(res.bc));
    out["cfg"] = nlohmann::json::parse(mplx::dump_cfg_json(res.bc));
    fmt::print("{}\n", out.dump());
    return 0;
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