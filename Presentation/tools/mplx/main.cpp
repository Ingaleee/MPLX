#include "../../../Application/mplx-compiler/compiler.hpp"
#include "../../../Application/mplx-vm/vm.hpp"
#include "../../../Domain/mplx-lang/lexer.hpp"
#include "../../../Domain/mplx-lang/parser.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  std::string mode;
  std::string fileArg;
  std::string src;

  auto print_usage = []() {
    std::cout << "Usage: mplx [--run|--check|--symbols] [--jit on|off|auto] <file>\n";
    std::ofstream("help.txt").write("Usage: mplx [--run|--check|--symbols] [--jit on|off|auto] <file>\n", 74);
  };

  if (argc >= 2)
    mode = argv[1];
  if (mode == "--help" || argc < 2) {
    print_usage();
    return 0;
  }
  if (mode == "--version") {
    std::cout << "mplx 0.2.0\n";
    std::ofstream("ver.txt") << "mplx 0.2.0\n";
    return 0;
  }
  // optional --jit flag
  std::string jitMode = "auto";
  int argi            = 2;
  if (argc >= 4 && std::string(argv[2]) == "--jit") {
    jitMode = argv[3];
    argi    = 4;
  }
  if ((mode == "--run" || mode == "--check" || mode == "--symbols") && argc > argi)
    fileArg = argv[argi];
  if ((mode == "--run" || mode == "--check" || mode == "--symbols") && fileArg.empty()) {
    print_usage();
    return 2;
  }
  if (!(mode == "--run" || mode == "--check" || mode == "--symbols")) {
    print_usage();
    return 2;
  }

  if (mode == "--run" || mode == "--check" || mode == "--symbols") {
    std::ifstream ifs(fileArg);
    if (!ifs) {
      if (mode == "--check") {
        std::string s = std::string("{\"diagnostics\": [{\"message\": \"cannot open file: ") + fileArg + "\", \"line\": 0, \"col\": 0}]}\n";
        std::cout << s;
        std::ofstream("check.json") << s;
        return 1;
      }
      std::string m = std::string("cannot open file: ") + fileArg + "\n";
      std::cout << m;
      std::ofstream("run.txt") << m;
      return 1;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    src = ss.str();
  } else {
    src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  }

  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();

  if (mode == "--check") {
    std::ostringstream os;
    os << "{\"diagnostics\": [";
    bool first = true;
    for (const auto &d : ps.diagnostics()) {
      if (!first)
        os << ", ";
      os << "{\"message\": \"" << d << "\", \"line\": 0, \"col\": 0}";
      first = false;
    }
    os << "]}\n";
    auto s = os.str();
    std::cout << s;
    std::ofstream("check.json") << s;
    return ps.diagnostics().empty() ? 0 : 1;
  }

  if (mode == "--symbols") {
    std::ostringstream os;
    os << "{\"functions\": [";
    bool first = true;
    for (const auto &f : mod.functions) {
      if (!first)
        os << ", ";
      os << "{\"name\": \"" << f.name << "\", \"arity\": " << f.params.size() << "}";
      first = false;
    }
    os << "]}\n";
    auto s = os.str();
    std::cout << s;
    std::ofstream("symbols.json") << s;
    return 0;
  }

  if (mode == "--run") {
    try {
      mplx::Compiler c;
      auto res = c.compile(mod);
      if (!res.diags.empty()) {
        std::ostringstream os;
        os << "Compilation errors:\n";
        for (const auto &d : res.diags)
          os << d << "\n";
        auto s = os.str();
        std::cout << s;
        std::ofstream("run.txt") << s;
        return 1;
      }

      mplx::VM vm(res.bc);
#if defined(MPLX_WITH_JIT)
      (void)jitMode; // placeholder for future JIT integration
#endif
      auto result = vm.run("main");
      std::ostringstream os;
      os << "Result: " << result << "\n";
      auto s = os.str();
      std::cout << s;
      std::ofstream("run.txt") << s;
      return 0;
    } catch (const std::exception &e) {
      std::ostringstream os;
      os << "Runtime error: " << e.what() << "\n";
      auto s = os.str();
      std::cout << s;
      std::ofstream("run.txt") << s;
      return 1;
    }
  }

  return 0;
}