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
  int hotThreshold    = 1;
  int argi            = 2;
  bool jitDump        = false;
  if (argc >= 4 && std::string(argv[2]) == "--jit") {
    jitMode = argv[3];
    argi    = 4;
  }
  if (argc >= argi + 1 && std::string(argv[argi]) == "--jit-dump") {
    jitDump = true;
    ++argi;
  }
  if (argc >= argi + 2 && std::string(argv[argi]) == "--hot" ) {
    hotThreshold = std::atoi(argv[argi+1]);
    argi += 2;
  }
  if ((mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--bench") && argc > argi)
    fileArg = argv[argi];
  if ((mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--bench") && fileArg.empty()) {
    print_usage();
    return 2;
  }
  if (!(mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--bench")) {
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
      if (jitDump) {
#if defined(_WIN32)
        _putenv_s("MPLX_JIT_DUMP", "1");
#else
        setenv("MPLX_JIT_DUMP", "1", 1);
#endif
      }
      if (jitMode == "off") vm.setJitMode(mplx::VM::JitMode::Off);
      else if (jitMode == "on") vm.setJitMode(mplx::VM::JitMode::On);
      else vm.setJitMode(mplx::VM::JitMode::Auto);
      vm.setHotThreshold(hotThreshold);
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

  if (mode == "--bench") {
    try {
      // defaults
      std::string benchMode = "compile-run"; // or "run-only"
      int runs = 20;
      bool jsonOut = true;
      // parse extra bench flags after file arg
      int opti = argi + 1;
      for (int i = opti; i + 0 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) { benchMode = argv[++i]; }
        else if (a == "--runs" && i + 1 < argc) { runs = std::max(1, atoi(argv[++i])); }
        else if (a == "--json") { jsonOut = true; }
      }

      mplx::Compiler c0;
      std::vector<double> timesMs; timesMs.reserve((size_t)runs);

      if (benchMode == "run-only") {
        auto cres = c0.compile(mod);
        for (int i = 0; i < runs; ++i) {
          auto t0 = std::chrono::high_resolution_clock::now();
          mplx::VM vm(cres.bc);
#if defined(MPLX_WITH_JIT)
          if (jitMode == "off") vm.setJitMode(mplx::VM::JitMode::Off);
          else if (jitMode == "on") vm.setJitMode(mplx::VM::JitMode::On);
          else vm.setJitMode(mplx::VM::JitMode::Auto);
          vm.setHotThreshold(hotThreshold);
#endif
          volatile auto rv = vm.run("main"); (void)rv;
          auto t1 = std::chrono::high_resolution_clock::now();
          timesMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
      } else {
        for (int i = 0; i < runs; ++i) {
          auto t0 = std::chrono::high_resolution_clock::now();
          mplx::Compiler c; auto cres = c.compile(mod);
          mplx::VM vm(cres.bc);
#if defined(MPLX_WITH_JIT)
          if (jitMode == "off") vm.setJitMode(mplx::VM::JitMode::Off);
          else if (jitMode == "on") vm.setJitMode(mplx::VM::JitMode::On);
          else vm.setJitMode(mplx::VM::JitMode::Auto);
          vm.setHotThreshold(hotThreshold);
#endif
          volatile auto rv = vm.run("main"); (void)rv;
          auto t1 = std::chrono::high_resolution_clock::now();
          timesMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
      }

      double sum = 0.0, best = 1e100, worst = -1.0;
      for (double v : timesMs) { sum += v; if (v < best) best = v; if (v > worst) worst = v; }
      double avg = sum / (double)timesMs.size();

      bool jitEnabled = false;
#if defined(MPLX_WITH_JIT)
      jitEnabled = (jitMode != "off");
#endif

      if (jsonOut) {
        std::ostringstream os;
        os << "{\"mode\": \"" << benchMode << "\", "
           << "\"runs\": " << runs << ", "
           << "\"avg_ms\": " << avg << ", "
           << "\"best_ms\": " << best << ", "
           << "\"worst_ms\": " << worst << ", "
           << "\"jit\": " << (jitEnabled ? "true" : "false")
           << "}\n";
        auto s = os.str();
        std::cout << s;
        std::ofstream("bench.json") << s;
      } else {
        std::cout << "mode=" << benchMode << " runs=" << runs
                  << " avg=" << avg << "ms best=" << best << "ms worst=" << worst
                  << " jit=" << (jitEnabled ? "on" : "off") << "\n";
      }
      return 0;
    } catch (const std::exception &e) {
      std::ostringstream os;
      os << "Bench error: " << e.what() << "\n";
      auto s = os.str();
      std::cout << s;
      std::ofstream("bench.txt") << s;
      return 1;
    }
  }

  return 0;
}