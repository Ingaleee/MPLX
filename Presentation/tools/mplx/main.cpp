#include "../../../Application/mplx-compiler/compiler.hpp"
#include "../../../Application/mplx-vm/vm.hpp"
#include "../../../Domain/mplx-lang/lexer.hpp"
#include "../../../Domain/mplx-lang/parser.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

static void write_text_atomic(const fs::path &target, const std::string &text) {
  std::error_code ec;
  if (auto parent = target.parent_path(); !parent.empty())
    fs::create_directories(parent, ec);
  fs::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
      throw std::runtime_error(std::string("cannot open temp file: ") + tmp.string());
    out.write(text.data(), (std::streamsize)text.size());
    out.flush();
    if (!out)
      throw std::runtime_error(std::string("cannot flush temp file: ") + tmp.string());
  }
#if defined(_WIN32)
  fs::remove(target, ec);
#endif
  fs::rename(tmp, target, ec);
  if (ec)
    throw std::runtime_error(std::string("rename failed: ") + tmp.string() + " -> " + target.string() + ": " + ec.message());
}

static void write_result_if_needed(const fs::path &inputPath,
                                   const fs::path &outPath,
                                   bool noRunFile,
                                   long long result) {
  if (noRunFile) {
    std::cerr << "[cli] no-runfile: skip writing\n";
    return;
  }
  try {
    fs::path target = outPath.empty()
                           ? (inputPath.has_parent_path() ? inputPath.parent_path() / "run.txt"
                                                           : fs::current_path() / "run.txt")
                           : outPath;
    std::cerr << "[cli] writing result to: " << target.string() << "\n";
    write_text_atomic(target, std::to_string(result) + "\r\n");
    std::cerr << "[cli] write ok\n";
  } catch (const std::exception &ex) {
    std::cerr << "write runfile failed: " << ex.what() << "\n";
  }
}

template <typename ModuleT>
static int handle_run(const ModuleT &mod,
                      const fs::path &inputPath,
                      const fs::path &outPath,
                      bool noRunFile,
                      bool jitVerify,
                      const std::string &jitMode,
                      int hotThreshold,
                      bool traceExec,
                      uint64_t traceLimit,
                      bool jitDump) {
  std::cerr << "[cli] enter --run\n";
  try {
    mplx::Compiler c;
    auto res = c.compile(mod);
    if (!res.diags.empty()) {
      std::ostringstream os;
      os << "Compilation errors:\n";
      for (const auto &d : res.diags) os << d << "\n";
      auto s = os.str();
      std::cout << s;
      if (!noRunFile) {
        try {
          fs::path target = outPath.empty() ? (inputPath.has_parent_path() ? inputPath.parent_path() / "run.txt" : fs::current_path() / "run.txt") : outPath;
          write_text_atomic(target, s);
        } catch (const std::exception &ex) {
          std::cerr << "write runfile failed: " << ex.what() << "\n";
        }
      }
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

    long long result = 0;
#if defined(MPLX_WITH_JIT)
    if (jitVerify) {
      mplx::VM vmInterp(res.bc);
      vmInterp.setJitMode(mplx::VM::JitMode::Off);
      vmInterp.setHotThreshold(hotThreshold);
      vmInterp.setTrace(traceExec);
      vmInterp.setTraceLimit(traceLimit);
      auto rInterp = vmInterp.run("main");
      vm.setTrace(traceExec);
      vm.setTraceLimit(traceLimit);
      auto rJit = vm.run("main");
      if (rInterp != rJit) {
        std::ostringstream verr;
        verr << "[JIT-VERIFY] mismatch: interp=" << rInterp << " jit=" << rJit << "\n";
        auto vs = verr.str();
        std::cout << vs;
        if (!noRunFile) {
          try {
            fs::path target = outPath.empty() ? (inputPath.has_parent_path() ? inputPath.parent_path() / "run.txt" : fs::current_path() / "run.txt") : outPath;
            write_text_atomic(target, vs);
          } catch (const std::exception &ex) {
            std::cerr << "write runfile failed: " << ex.what() << "\n";
          }
        }
        return 3;
      }
      result = rJit;
      std::cerr << "[cli] ran: " << result << "\n";
      write_result_if_needed(inputPath, outPath, noRunFile, result);
      std::cout << "Result: " << result << "\n";
      return 0;
    } else {
      vm.setTrace(traceExec);
      vm.setTraceLimit(traceLimit);
      result = vm.run("main");
    }
#else
    vm.setTrace(traceExec);
    vm.setTraceLimit(traceLimit);
    result = vm.run("main");
#endif
    std::cerr << "[cli] ran: " << result << "\n";
    write_result_if_needed(inputPath, outPath, noRunFile, result);
    std::cout << "Result: " << result << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::ostringstream os;
    os << "Runtime error: " << e.what() << "\n";
    auto s = os.str();
    std::cout << s;
    if (!noRunFile) {
      try {
        fs::path target = outPath.empty() ? (inputPath.has_parent_path() ? inputPath.parent_path() / "run.txt" : fs::current_path() / "run.txt") : outPath;
        write_text_atomic(target, s);
      } catch (const std::exception &ex) {
        std::cerr << "write runfile failed: " << ex.what() << "\n";
      }
    }
    return 1;
  }
}

int main(int argc, char **argv) {
  std::string mode;
  std::string fileArg;
  std::string src;
  fs::path outPath;    
  bool noRunFile = false; 
  bool jitVerify = false; 
  bool traceExec = false;
  uint64_t traceLimit = 0;

  auto print_usage = []() {
    const char *u = "Usage: mplx [--run|--check|--symbols|--bench] [--jit on|off|auto] [--jit-dump] [--hot N] [--jit-verify] [--trace] [--trace-limit N] [--out PATH] [--no-runfile] <file>\n";
    std::cout << u;
    std::ofstream("help.txt").write(u, (std::streamsize)std::char_traits<char>::length(u));
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
  // Robust flag parsing independent of order; support --out=PATH
  std::string jitMode = "auto";
  int hotThreshold    = 1;
  bool jitDump        = false;
  if (argc < 3) {
    print_usage();
    return 2;
  }
  // Collect args excluding the initial mode
  std::vector<std::string> args;
  for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
  std::vector<std::string> positional;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &a = args[i];
    if (a.rfind("--out=", 0) == 0) { outPath = fs::path(a.substr(6)); continue; }
    if (a == "--out" && i + 1 < args.size()) { outPath = fs::path(args[++i]); continue; }
    if (a == "--no-runfile") { noRunFile = true; continue; }
    if (a == "--jit-dump") { jitDump = true; continue; }
    if (a == "--jit" && i + 1 < args.size()) { jitMode = args[++i]; continue; }
    if (a == "--hot" && i + 1 < args.size()) { hotThreshold = std::atoi(args[++i].c_str()); continue; }
    if (a == "--jit-verify") { jitVerify = true; continue; }
    if (a == "--trace") { traceExec = true; continue; }
    if (a == "--trace-limit" && i + 1 < args.size()) { traceLimit = (uint64_t)std::strtoull(args[++i].c_str(), nullptr, 10); continue; }
    // Non-flag -> positional (candidate input)
    if (!a.empty() && a[0] != '-') positional.push_back(a);
  }
  if (!(mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--bench")) {
    print_usage();
    return 2;
  }
  if (!positional.empty()) fileArg = positional.back();
  if ((mode == "--run" || mode == "--check" || mode == "--symbols" || mode == "--bench") && fileArg.empty()) {
    print_usage();
    return 2;
  }

  // Normalize --out path early and ensure parent directory exists
  if (!outPath.empty()) {
    std::error_code ec;
    outPath = fs::absolute(outPath);
    auto parent = outPath.parent_path();
    if (!parent.empty()) {
      fs::create_directories(parent, ec);
    }
  }

  try {
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    std::cerr << "[cli] cwd:   " << (ec ? std::string("<err>") : cwd.string()) << "\n";
  } catch(...) {}
  try {
    std::cerr << "[cli] input: " << fs::path(fileArg).string() << "\n";
  } catch(...) {}
  if (!outPath.empty()) {
    std::cerr << "[cli] --out: " << outPath.string() << "\n";
  }
  std::cerr << "[cli] build-id: run-write-v2\n";

  if (mode == "--run" || mode == "--check" || mode == "--symbols") {
    std::ifstream ifs(fileArg);
    if (!ifs) {
      if (mode == "--check") {
        std::string s = std::string("{\"diagnostics\": [{\"message\": \"cannot open file: ") + fileArg + "\", \"line\": 0, \"col\": 0}]}\n";
        std::cout << s;
        // keep legacy behavior for check.json in CWD
        std::ofstream("check.json") << s;
        return 1;
      }
      std::string m = std::string("cannot open file: ") + fileArg + "\n";
      std::cout << m;
      if (!noRunFile) {
        try {
          fs::path inputPath = fs::path(fileArg);
          fs::path target = outPath.empty() ? (inputPath.has_parent_path() ? inputPath.parent_path() / "run.txt" : fs::current_path() / "run.txt") : outPath;
          write_text_atomic(target, m);
        } catch (const std::exception &ex) {
          std::cerr << "write runfile failed: " << ex.what() << "\n";
        }
      }
      return 1;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    src = ss.str();
  } else {
    src = "fn main() -> i32 { let x = 1 + 2; return x; }";
  }

  std::cerr << "[cli] before lex\n";
  mplx::Lexer lx(src);
  auto toks = lx.Lex();
  std::cerr << "[cli] after lex\n";
  std::cerr << "[cli] before parse\n";
  mplx::Parser ps(std::move(toks));
  auto mod = ps.parse();
  std::cerr << "[cli] after parse\n";

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
    std::cerr << "[cli] dispatch --run\n";
    return handle_run(mod,
                      fs::path(fileArg),
                      outPath,
                      noRunFile,
                      jitVerify,
                      jitMode,
                      hotThreshold,
                      traceExec,
                      traceLimit,
                      jitDump);
  }

  if (mode == "--bench") {
    try {
      // defaults
      std::string benchMode = "compile-run"; // or "run-only"
      int runs = 20;
      bool jsonOut = true;
      // parse extra bench flags (disabled for now due to reordered arg parsing)
      int opti = argc; // no-op
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