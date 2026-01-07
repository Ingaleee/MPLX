# Validation of MPLX 0.5

Local validation: 2026-10-13 (Europe/Moscow), modified working tree based on
d91ac59. GitHub jobs have not been published or remotely executed.
[The previous 0.4 report](VALIDATION_0.4.md) preserves the earlier results.

## Executed configurations

Windows x64: MSVC 19.44.35228, CMake 3.31.6, Ninja.
Linux x64: Ubuntu 24.04 container on Docker Desktop/WSL2, Clang 18.1.3,
CMake 3.28.3; image built with scripts/linux/Dockerfile.
Core builds completed without C/C++ compiler warnings.

| Configuration                           | JIT | Result                                       |
| --------------------------------------- | --- | -------------------------------------------- |
| Windows Debug                           | OFF | 20/20 CTest suites passed                    |
| Windows Debug                           | ON  | 20/20 CTest suites passed                    |
| Windows Release                         | ON  | 20/20 CTest suites passed                    |
| Linux Release                           | ON  | 20/20 CTest suites passed                    |
| Linux Debug + ASan/UBSan/leak detection | ON  | 20/20 CTest suites passed                    |
| Linux Debug + ThreadSanitizer           | ON  | Shared-program and embedding-contexts passed |
| .NET/native Windows                     | ON  | 226 checks passed                            |
| .NET/native Linux                       | ON  | 226 checks passed                            |

CTest includes module format, shared program, embedding contexts, a separately
compiled C11 consumer, typed IR, native calls, frontend, compiler, bytecode
verification, runtime, differential programs, legacy C ABI, three deterministic
fuzz-seed runners, LSP stdio/IPC, compiler client/service and CLI integration.
Editor tests require built LSP dependencies at CMake configuration time.

New format tests pass 5,803 checks: an independently encoded golden file,
canonical serialization, signed little-endian constants, typed export calls,
source traps after round trip, every truncation point, 2,000 corruption mutations
and metadata/bytecode/source-map mutations with repaired checksums.
Shared-program tests start eight workers together, execute 800 independent calls
and require exactly one native compilation on x64. Tests also compare host
argument results/resource peaks, reject invalid i32/bool representations, enforce
pre-entry argument budgets and guard export return representations.

Embedding tests retain instances after program-handle release, verify save/load,
interleave independent instances, collect source-mapped faults and stats, race
cancellation with a live loop, reject concurrent invoke/reset with BUSY and
reuse instances after faults/cancellation reset. .NET adds SafeHandle disposal,
parallel workers and structured exception checks. The sample invokes transform
with typed parameters and returns 42 with real native execution.

Legacy compiler/native suites still cover 1,000 generated expressions, 100
control-flow programs and 100 call programs, 5,000 malformed bytecode mutations,
precise resource limits and 6,000 recursive language frames without host recursion.

## Sanitizers and fuzzing

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;
TSAN_OPTIONS=halt_on_error=1.

Docker's default seccomp profile prevented TSan's startup personality call.
The two concurrency suites passed in the same network-disabled/read-only-source
container with --security-opt seccomp=unconfined. No host-wide ASLR/sysctl setting
was changed. LLVM documents this startup interaction in its
[TSan platform change](https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20240624/1394004.html).
The TSan result covers instrumented C++ helpers/publication/cancellation;
generated machine code itself is not instrumented.

Clang's function-sanitizer exception remains limited to the generated-entry
wrapper. ASan and other UBSan checks remain active in runtime helpers. Module
fuzzing compares values/errors/function/PC/source byte ranges and resource peaks
between bounded interpreter/native execution.

| libFuzzer target                         | Requested time | Executions | Actual duration |
| ---------------------------------------- | -------------- | ---------- | --------------- |
| Source                                   | 30 s           | 4,480      | 31 s            |
| Raw bytecode                             | 30 s           | 101,841    | 31 s            |
| Portable module, including repaired CRCs | 30 s           | 40,492     | 32 s            |

Maximum input lengths: 2,048 / 1,024 / 16,384 bytes; per-input timeout five
seconds. All campaigns exited successfully without crash/leak/UB reports.
These bounded smoke campaigns are not exhaustive coverage. Mutable corpora and
full logs stay under build/linux; fuzz-validation05.log records this run.

Reproduce with an installed Clang runtime:

```sh
sh scripts/linux/validate.sh release
sh scripts/linux/validate.sh asan
sh scripts/linux/validate.sh tsan
sh scripts/linux/validate.sh fuzz
sh scripts/linux/validate-dotnet.sh
```

Docker mounts the repository at /workspace read-only and build/linux read-write.
TSan requires its startup personality call to be allowed by the test environment.
.NET 8 reference packs must be available or restored; local Windows checks used
the cached build/nuget directory. Linux .NET checks passed with the cached packs,
though blocked network access produced NU1900 audit-feed warnings.

## Installed artifacts and portability

Local artifacts:

- build/mplx-0.5.0-windows-x64.zip
- build/linux/mplx-0.5.0-linux-x64.tar.gz
- build/mplx-0.5.0.vsix

Windows/Linux packages contain CLI, native C library, public header, examples,
wire/API documentation and an example portable module. The installed Windows
CLI compiles embedding.mplx; the installed Linux CLI executes that Windows-built
module with --entry transform --arg 21 --arg 2 --arg 1, returns 42, reports one
native execution/no fallback and passes interpreter/native verification.
The independently built Windows/Linux embedding modules are byte-identical.

The VSIX's extracted server is checked through its IPC protocol with bundled
runtime dependencies. Server/extension TypeScript builds pass. Artifact sizes
and SHA-256 values are recorded by scripts/artifact-manifest.mjs in
build/artifact-manifest.json; CI now creates per-job archives/manifests too.

Windows requires the Visual C++ x64 runtime. Linux binaries target the container's
glibc/libstdc++ environment. Old benchmark files remain measurements of 0.4;
no new performance claim is made from these correctness checks.

## Remaining limits

macOS and GitHub-hosted jobs remain unexecuted. Native execution requires x64;
cancellation/tracing and unsupported native shapes interpret. The bytecode
verifier establishes structural/stack safety; full source-type/definite-assignment
proofs belong to typed IR, with argument/result representation checks at the host
boundary. CRC is a corruption check, not authentication. Cross-file imports,
workspace indexing and richer values are the next roadmap milestones.
