#include "capi.hpp"
#include "../../Domain/mplx-lang/lexer.hpp"
#include "../../Domain/mplx-lang/parser.hpp"
#include "../../Application/mplx-compiler/compiler.hpp"
#include "../../Application/mplx-vm/vm.hpp"
#include <cstring>
#include <string>
#include <new>

static char* dup_utf8(const std::string& s){
  char* p = (char*)::malloc(s.size()+1);
  if (!p) return nullptr;
  std::memcpy(p, s.data(), s.size());
  p[s.size()] = '\0';
  return p;
}

extern "C" {

MPLX_API void mplx_free(char* ptr){ if(ptr) ::free(ptr); }

MPLX_API int mplx_check_source(const char* source_utf8, char** out_json, char** out_error){
  if (!source_utf8 || !out_json || !out_error) return 1;
  *out_json = nullptr; *out_error = nullptr;
  try{
    std::string src(source_utf8);
    mplx::Lexer lex(src); auto toks = lex.Lex();
    mplx::Parser p(toks); auto mod = p.parse();
    
    std::string out = "{\"diagnostics\": [";
    bool first = true;
    for (auto& d : p.diagnostics()){
      if(!first) out += ", ";
      out += "\"" + d + "\"";
      first = false;
    }
    out += "]}";
    
    *out_json = dup_utf8(out);
    if (!*out_json) { *out_error = dup_utf8("alloc failure"); return 2; }
    return 0;
  } catch (const std::exception& ex){
    *out_error = dup_utf8(ex.what());
    return 3;
  } catch (...) {
    *out_error = dup_utf8("unknown error");
    return 3;
  }
}

MPLX_API int mplx_run_from_source(const char* source_utf8, const char* entry_utf8, long long* out_result, char** out_error){
  if (!source_utf8 || !entry_utf8 || !out_result || !out_error) return 1;
  *out_error = nullptr;
  try{
    std::string src(source_utf8);
    std::string entry(entry_utf8);
    mplx::Lexer lex(src); auto toks = lex.Lex();
    mplx::Parser p(toks); auto mod = p.parse();
    mplx::Compiler c; auto res = c.compile(mod);
    if (!res.diags.empty()){
      std::string error = "{\"compile\": [";
      bool first = true;
      for (auto& d : res.diags){
        if(!first) error += ", ";
        error += "\"" + d + "\"";
        first = false;
      }
      error += "]}";
      *out_error = dup_utf8(error);
      return 4;
    }
    mplx::VM vm(res.bc);
    long long rv = vm.run(entry);
    *out_result = rv;
    return 0;
  } catch (const std::exception& ex){
    *out_error = dup_utf8(ex.what());
    return 3;
  } catch (...) {
    *out_error = dup_utf8("unknown error");
    return 3;
  }
}

} // extern "C"