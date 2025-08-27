$ErrorActionPreference = "Stop"

# 0) Патч parser.cpp (убираем designator-init у Function, чтобы MSVC не вредничал)
if (Test-Path "src-cpp/mplx-lang/parser.cpp") {
  $parser = Get-Content "src-cpp/mplx-lang/parser.cpp" -Raw
  $parser2 = $parser -replace 'Function f\{\.name=name,\.params=params,\.returnType=retType\};', @"
Function f;
f.name = name;
f.params = params;
f.returnType = retType;
"@
  if ($parser -ne $parser2) { Set-Content -Encoding utf8 "src-cpp/mplx-lang/parser.cpp" $parser2 }
}

# 1) Добавим подпроект C API в root CMake
if (Test-Path "CMakeLists.txt") {
  $root = Get-Content "CMakeLists.txt" -Raw
  if ($root -notmatch "src-cpp/mplx-capi") {
    $root += @"

add_subdirectory(src-cpp/mplx-capi)
"@
    Set-Content -Encoding utf8 "CMakeLists.txt" $root
  }
}

# 2) Директории
$dirs = @(
 "src-cpp/mplx-capi",
 "src/Mplx.DotNet",
 "src/Mplx.DotNet.Sample"
)
$dirs | ForEach-Object { New-Item -Force -ItemType Directory $_ | Out-Null }

# 3) C API: CMakeLists + capi.hpp + capi.cpp
@'
add_library(mplx-capi SHARED capi.cpp)
find_package(nlohmann_json CONFIG REQUIRED)

# Имя выходной DLL
set_target_properties(mplx-capi PROPERTIES OUTPUT_NAME "mplx_native")

target_include_directories(mplx-capi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler ../mplx-lang ../mplx-vm)
target_link_libraries(mplx-capi PUBLIC mplx-lang mplx-compiler mplx-vm nlohmann_json::nlohmann_json)
'@ | Out-File -Encoding utf8 src-cpp/mplx-capi/CMakeLists.txt -NoNewline

@'
#pragma once
#include <cstdint>

#if defined(_WIN32)
  #define MPLX_API __declspec(dllexport)
#else
  #define MPLX_API __attribute__((visibility("default")))
#endif

extern "C" {
  // Возвращает 0 при успехе. При ошибке: *out_error -> utf8 строка (надо вызвать mplx_free).
  MPLX_API int mplx_run_from_source(const char* source_utf8, const char* entry_utf8, long long* out_result, char** out_error);
  // JSON с diagnostics (массив строк). 0 при успехе; *out_json -> utf8 (нужно mplx_free). *out_error при сбое.
  MPLX_API int mplx_check_source(const char* source_utf8, char** out_json, char** out_error);
  // Освобождение строк, выделенных нативной библиотекой.
  MPLX_API void mplx_free(char* ptr);
}
'@ | Out-File -Encoding utf8 src-cpp/mplx-capi/capi.hpp -NoNewline

@'
#include "capi.hpp"
#include "../mplx-lang/lexer.hpp"
#include "../mplx-lang/parser.hpp"
#include "../mplx-compiler/compiler.hpp"
#include "../mplx-vm/vm.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <string>
#include <new>

using json = nlohmann::json;

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
    json out; out["diagnostics"] = json::array();
    for (auto& d : p.diagnostics()) out["diagnostics"].push_back(d);
    auto s = out.dump();
    *out_json = dup_utf8(s);
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
      json j; j["compile"] = res.diags;
      *out_error = dup_utf8(j.dump());
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
'@ | Out-File -Encoding utf8 src-cpp/mplx-capi/capi.cpp -NoNewline

# 4) .NET обёртка (Class Library)
@'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
</Project>
'@ | Out-File -Encoding utf8 src/Mplx.DotNet/Mplx.DotNet.csproj -NoNewline

@'
using System;
using System.Runtime.InteropServices;

namespace Mplx.DotNet;

public static class MplxRuntime
{
    private const string Dll = "mplx_native";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_run_from_source")]
    private static extern int _RunFromSource(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string entryUtf8,
        out long result,
        out IntPtr errorUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_check_source")]
    private static extern int _CheckSource(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceUtf8,
        out IntPtr jsonUtf8,
        out IntPtr errorUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_free")]
    private static extern void _Free(IntPtr ptr);

    private static string? PtrToUtf8AndFree(IntPtr p)
    {
        if (p == IntPtr.Zero) return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { _Free(p); }
    }

    public static long RunFromSource(string source, string entry = "main")
    {
        var rc = _RunFromSource(source, entry, out var result, out var errPtr);
        var err = PtrToUtf8AndFree(errPtr);
        if (rc != 0) throw new InvalidOperationException($"mplx_run_from_source rc={rc}: {err}");
        return result;
    }

    public static string CheckSource(string source)
    {
        var rc = _CheckSource(source, out var jsonPtr, out var errPtr);
        var json = PtrToUtf8AndFree(jsonPtr);
        var err  = PtrToUtf8AndFree(errPtr);
        if (rc != 0) throw new InvalidOperationException($"mplx_check_source rc={rc}: {err}");
        return json ?? "{}";
    }
}
'@ | Out-File -Encoding utf8 src/Mplx.DotNet/MplxRuntime.cs -NoNewline

# 5) .NET пример (Console)
@'
<Project Sdk="Microsoft.NET.Sdk">
  <ItemGroup>
    <ProjectReference Include="..\Mplx.DotNet\Mplx.DotNet.csproj" />
  </ItemGroup>
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
</Project>
'@ | Out-File -Encoding utf8 src/Mplx.DotNet.Sample/Mplx.DotNet.Sample.csproj -NoNewline

@'
using System;
using System.IO;
using Mplx.DotNet;

class Program
{
    static void Main(string[] args)
    {
        var file = args.Length > 0 ? args[0] : Path.Combine("examples", "hello.mplx");
        var src  = File.ReadAllText(file);

        Console.WriteLine("== Check ==");
        var diag = MplxRuntime.CheckSource(src);
        Console.WriteLine(diag);

        Console.WriteLine("== Run ==");
        var rv = MplxRuntime.RunFromSource(src, "main");
        Console.WriteLine($"Return: {rv}");
    }
}
'@ | Out-File -Encoding utf8 src/Mplx.DotNet.Sample/Program.cs -NoNewline

# 6) Git коммит
git add -A
git commit -m "feat(dotnet): add C API (mplx_native) and .NET P/Invoke wrapper with sample"


