# Validation of MPLX 0.4

Local validation: 2026-10-12 to 2026-10-13 (Europe/Moscow). Starting commit: d91ac59.
These results describe the modified working tree and local artifacts;
the GitHub workflow has not been published or remotely executed.

## Completed configurations

Windows x64: MSVC 19.44.35228, CMake 3.31.6, Ninja, Intel Core Ultra 9 185H.
Linux x64: Ubuntu 24.04 container on Docker Desktop/WSL2, Clang 18.1.3,
CMake 3.28.3. The image is built by scripts/linux/Dockerfile.

| Platform/configuration                  | JIT | CTest     | Result |
| --------------------------------------- | --- | --------- | ------ |
| Windows Debug                           | OFF | 15 suites | Passed |
| Windows Debug                           | ON  | 15 suites | Passed |
| Windows Release                         | ON  | 15 suites | Passed |
| Linux Release                           | ON  | 15 suites | Passed |
| Linux Debug + ASan/UBSan/leak detection | ON  | 15 suites | Passed |

The final C++ builds produced no compiler warnings.

Suites: typed IR, native calls, frontend, compiler, bytecode verifier, runtime,
generated differential programs, C ABI, two portable fuzz-seed runners,
LSP stdio, LSP IPC, bounded compiler client, framed compiler service and CLI.
LSP suites are added by CMake when its dependencies and compiled server exist.

Coverage includes malformed types/IR opcodes, dominance, definite assignment,
source-map ordering/boundaries, scoped symbol identity, typed narrowing/overflow,
unreachable-source diagnostics, optimized/unoptimized values/traps, VM reuse,
budgets/cancellation and string ownership. Generated tests exercise 1,000
expressions and 100 control-flow programs, plus 100 generated call programs.
The verifier processes 5,000 deterministic malformed-module mutations.

Native tests require real execution on x64 and compare result/error, function/PC,
instructions, observed operands/locals and frames. They include factorial,
Fibonacci, mutual recursion, failures in callees, exact resource limits and
6,000 recursive language frames using one host entry frame.

LSP process tests exercise scope-aware completion, hover, definitions/references,
capture-safe rename, signature recovery, symbols, semantic tokens, Unicode/CRLF,
rapid edits and close cleanup. Client tests cover 64-request/16-MiB queue limits,
draining, source-size rejection and compiler restart.

## Sanitizers and fuzzing

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 and
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.

Clang's indirect function check initially read nonexistent type metadata before
the generated JIT entry. The fix suppresses only that check in a noinline entry
wrapper; ASan and other UBSan checks remain enabled. The full native suite now
passes. Generated machine code is not itself ASan-instrumented; verifier facts,
runtime bounds and differential tests remain part of its correctness evidence.

Two coverage-guided Clang/libFuzzer smoke campaigns completed successfully:

| Target                                               | Requested budget | Completed executions | Actual fuzzer duration |
| ---------------------------------------------------- | ---------------- | -------------------- | ---------------------- |
| Source (optimized/raw/interpreter/native comparison) | 30 s             | 3,295                | 31 s                   |
| Malformed bytecode                                   | 30 s             | 150,747              | 32 s                   |

Inputs were limited to 2,048/1,024 bytes with a five-second per-input timeout.
The full local log is build/linux/fuzz-validation.log. No crash, leak or
undefined-behavior report was found in these bounded campaigns; this is not
exhaustive coverage. Mutable corpora stay under build/linux.

Reproduce Linux checks with a compiler installed locally:

```sh
sh scripts/linux/validate.sh release
sh scripts/linux/validate.sh asan
sh scripts/linux/validate.sh fuzz
```

Or build the isolated image and mount the repository read-only plus build/linux
read-write. Tests/fuzzing need no network after image creation. Linux .NET
validation uses scripts/linux/validate-dotnet.sh and a previously built Release
library; SDK reference-pack restore may require nuget.org.

## Embedding and artifacts

.NET P/Invoke passed 214 checks on both Windows and Linux with fresh native
libraries: repeated calls, typed calls/conversions, diagnostics/source ranges,
arithmetic errors, bounded recursion/instructions, options ABI and embedded-NUL
rejection. NuGet.Config uses the public official feed.

Local artifacts: build/mplx-0.4.0.vsix and
build/mplx-0.4.0-windows-x64.zip. The installed Windows CLI returns 17711 with
57,326 native calls, no fallback and matching interpreter/native verification.
The server extracted from the VSIX passes the IPC protocol suite using only its
bundled runtime dependencies. TypeScript server and extension builds pass.

SHA-256 hashes are recorded separately in build/artifact-manifest.json.
Windows binaries require the Visual C++ x64 runtime. Linux artifacts target the
validation container's glibc/libstdc++ environment; source builds support other
toolchains. Optional historical ORM/network/package targets are outside these
core results.

## Benchmark evidence

benchmarks/windows-msvc-x64.json and windows-msvc-native-calls.json contain
50 interpreter and 50 native samples each after five warmups, actual native
execution/call counts, source hashes and CPU/OS/compiler/build/version metadata.
They measure the loop and call examples on one machine. Native comparison is
valid only when all sampled runs use native execution.

```sh
node scripts/benchmark.mjs build/release/bin/mplx examples/native_loop.mplx build/loop.json 50
node scripts/benchmark.mjs build/release/bin/mplx examples/native_calls.mplx build/calls.json 50
```

Use mplx.exe on Windows.

## Still unverified

macOS and GitHub-hosted jobs have not run. CI defines the cross-platform core,
LSP/extension build and packaging, sanitizer/fuzz and .NET jobs, with pinned
actions and retained install/VSIX artifacts. This local validation does not
establish a passing remote workflow. Cross-file imports/indexing, module
serialization and richer value ownership remain in ENGINEERING_ROADMAP.md.
