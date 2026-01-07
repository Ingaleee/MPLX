# Validation of MPLX 0.6

Local date: 2026-10-13 (Europe/Moscow). Results describe the modified working tree
based on d91ac59; GitHub-hosted jobs have not been published or executed.
Previous reports remain in VALIDATION_0.4.md and VALIDATION_0.5.md.

## Ultimate roadmap foundation package

The next working-tree package implements scalar semantics v1, reusable analyses,
checked preservation, transitive effect/range proofs, canonical IR snapshots and
isolated production-pass replay. Scope and remaining work are recorded in
[the M01–M65 ledger](vision/engineering-roadmap.md). No full M01–M65 completion
is claimed. Package/ABI/wire versions remain unchanged and previous release
archives have not been regenerated.

Executed final checks on 2026-10-13:

| Configuration                                      | Result                                                                                  |
| -------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Windows MSVC Release, JIT enabled                  | 35/35 CTest suites passed                                                               |
| Windows MSVC Debug, JIT disabled                   | 34/34 CTest suites passed                                                               |
| Linux Clang Debug/JIT, ASan/UBSan/leak detection   | 35/35 CTest suites passed                                                               |
| .NET solution against fresh Windows native library | Zero warnings/errors; 235 interop checks passed                                         |
| Normative scalar conformance                       | 16 rules, 52 authored cases; every intermediate pass outcome and replay checked         |
| Analysis/replay suite                              | 48,909 assertions, including independent graph oracles and signed boundary range proofs |
| Conformance suite with JIT                         | 11,786 assertions, including 120 generated programs with three metamorphisms each       |

Assertion counts include repeated cross-engine and boundary comparisons; they
are not counts of distinct scenarios. The optional seed checkpoint writer permits
retaining the last input before a native crash. The snapshot corpus currently
contains three inputs and executes 603 deterministic seed/mutation cases.

Six coverage-guided smoke campaigns completed successfully:

| Harness               | Executions | Reported duration |
| --------------------- | ---------: | ----------------: |
| Source                |      1,949 |              31 s |
| Raw bytecode          |     12,398 |              31 s |
| Portable module       |     54,155 |              31 s |
| Workspace             |        863 |              31 s |
| Mutated IR            |      1,029 |              34 s |
| Canonical IR snapshot |     40,007 |              31 s |

Each requested 30 seconds and used the accumulated local corpus. No crash, UB or
leak remained in the final campaigns. Generated x64 instructions themselves are
not instrumented by ASan. These short campaigns are regression evidence, not a
proof of absence of defects.

The initial snapshot seed suite exposed an out-of-bounds access in temporary-slot
allocation: a jump's unused `value` field passed IR verification and was later
treated as a genuine use. The verifier now rejects noncanonical jump/return
metadata and the lowerer inspects operand fields by terminator kind. The retained
`invalid-jump-value.mplxir` corpus input and explicit tests cover this defect. The
final sanitizer and fuzz results above include the repair.

Logs remain in ignored `build/foundations-windows-release.log`,
`build/foundations-windows-debug.log`, `build/foundations-linux-final.log`,
`build/foundations-dotnet.log` and `build/foundations-fuzz.log`.
The earlier failed sanitizer log remains in `build/foundations-linux.log`.
The checked-in [run manifest](verification/foundations-run.json) records exact
configuration results, log digests, source fingerprint and the pending measurement.

The compiler benchmark now builds with six workloads and a fifth execution mode,
raw register-native code. It includes a range-proved total-call fixture and records
native call counts, raw/optimized native bytes and compilation latency. A fresh
100-sample Windows measurement was **not executed**: automatic approval review
could not complete because its service usage limit was exhausted. No new speedup
or compile-latency claim is based on that pending measurement. Earlier retained
benchmark JSON below describes the preceding implementation.

macOS and hosted GitHub jobs remain unexecuted locally. The sixth harness is wired
into CI and the local Docker validation script. Source formatting uses the existing
repository-wide tools, including generated conformance C++.

## Post-0.6 compiler milestone

The SSA/native working-tree extension is documented in COMPILER_PIPELINE.md and
ADR 0009. Existing 0.6 package files below are earlier artifacts; they have not
been repackaged to contain this extension. No macOS or GitHub-hosted execution
has been performed locally for this compiler milestone.

Executed checks on 2026-10-13:

- Windows MSVC Release/JIT: 31/31 CTest suites passed.
- Windows MSVC Debug without JIT: 30/30 CTest suites passed; the native-specific
  machine suite is excluded in this configuration.
- Linux Clang Debug/JIT with ASan/UBSan/leak detection: 31/31 suites passed,
  including the coalescing allocator. Generated x64 is not itself instrumented.
- Corrected Mplx.sln builds the maintained .NET binding, sample and interop runner
  against the fresh Windows native library with zero warnings. The runner passes
  all 235 integration checks.
- The new SSA suite passes 4,868 internal checks; the independent AST suite passes
  9,950 after continuation-budget hardening. The native suite passes 51,708 checks, including 0–4 registers, pressure,
  relocation, alias backings, resource counters and 6000 explicit frames. These
  counts include repeated assertions across generated cases, not distinct test
  scenarios.

Logs are retained in ignored build/compiler-windows-release.log,
build/compiler-windows-debug.log and build/compiler-linux-coalesced.log.
Later narrow checks for strengthened source-span fuzz assertions supplement the
full runs. Formatting is checked through the same repository-wide tools.
The seven affected AST/incremental/fuzz suites were rerun after continuation
bounds and exact arithmetic source-span comparison on Windows Release, Windows
Debug without JIT and Linux ASan/UBSan; all seven passed on every configuration.
Two further 15-second source/workspace campaigns passed 1,187 and 529 executions
(16 seconds reported each), including the tightened comparisons.

Five coalescing-backend libFuzzer campaigns requested 30 seconds each and exited
successfully without a crash, leak or UB report. The final recorded campaign set
ran source 1,887 times, raw bytecode 10,010, container 42,790, workspace 1,614 and
mutated IR 1,076; each reported 31 seconds. It reused the accumulated local
corpora and is smoke evidence rather than an exhaustive campaign. Logs and
possible future crash artifacts remain under build/, with explicit artifact
prefixes keeping the mounted source tree read-only during local fuzzing.

The benchmark executes five embedded workloads, three warmups and 50 samples per
mode, with deterministic interleaving. Raw VM and SSA VM instruction counts are
checked against both native allocation modes. Cold compiler/native generation
timings are single samples. One Windows Release run on an Intel Core Ultra 9 185H
recorded these instruction counts:

| Workload            | Raw VM instructions | SSA instructions | Reduction |
| ------------------- | ------------------: | ---------------: | --------: |
| Numeric loop        |             430,031 |          250,020 |     41.9% |
| Branch swaps        |             995,043 |          585,035 |     41.2% |
| Small calls         |             168,031 |          114,020 |     32.1% |
| Recursive Fibonacci |             292,628 |          238,285 |     18.6% |
| Common values       |             740,031 |          420,020 |     43.2% |

The first allocator reduced static memory traffic but made some loops slower.
Copy coalescing was then added and retested. The ensuing local interleaved
50-sample run recorded arena/register medians of 0.1515/0.1328 ms (numeric loop),
0.3777/0.3412 ms (branch swaps), 0.1419/0.1334 ms (small calls),
0.3460/0.3344 ms (Fibonacci), and 0.2491/0.2106 ms (common values).
These small timings are sensitive to host load and power state; they are
workload-specific evidence, not a general speedup guarantee. All five retained
the same instruction budgets between VM and native modes. Native code size and
static reads/writes are recorded beside execution samples.

Reproduce with scripts/compiler-benchmark.mjs. Its report adds CPU/OS details,
Git state and executable/native-source SHA-256 hashes; measured samples and
source text are retained. Local reports are under build/compiler-benchmark*.json
and build/compiler-report.json. CI now produces this report for each core matrix
configuration; those jobs remain unexecuted here.

A final 100-sample Windows report, with the full environment, source text and
hashes, is retained in
[compiler-pipeline-windows.json](benchmarks/compiler-pipeline-windows.json).

## Completed checks

Windows x64: MSVC 19.44.35228 / CMake 3.31.6 / Ninja.
Linux x64: Ubuntu 24.04 Docker/WSL2 / Clang 18.1.3 / CMake 3.28.3.

| Configuration                           | JIT | Result             |
| --------------------------------------- | --- | ------------------ |
| Windows Debug                           | OFF | 27/27 CTest passed |
| Windows Debug                           | ON  | 27/27 CTest passed |
| Windows Release                         | ON  | 27/27 CTest passed |
| Linux Release                           | ON  | 27/27 CTest passed |
| Linux Debug + ASan/UBSan/leak detection | ON  | 27/27 CTest passed |
| .NET / Windows native library           | ON  | 235 checks passed  |
| .NET / Linux native library             | ON  | 235 checks passed  |

Core C/C++ builds produced no compiler warnings. Final graph-width/expanded-
interface guards, path-resolution reuse and frame-header bounds were validated
with follow-up module/service/LSP suites on Windows Release and Linux
Release/ASan. Exact logs remain under build/. Formatting tooling and the packaged
grammar were checked separately.

The seven additional suites cover source modules, incremental compilation,
dependency graphs, deterministic workspace fuzz seeds, cross-file LSP over
stdio/IPC and the persistent workspace service. Existing typed IR, verifier,
runtime/native, portable format, C ABI, differential and editor suites still run.

Module scenarios include explicit exports/private implementation calls, qualified
symbol identity, diamond graph deduplication, cycles, deleted dependencies,
root escapes, dependency depth and width budgets, linked portable execution,
dependency traps and source ranges. Incremental tests distinguish unchanged
builds (same VerifiedModule), body-only edits, exported signature changes,
foreign declaration movement, overlays and overlay closure. Results expose real
parsed/checked/hit counts rather than inferred cache behavior.

Cross-file LSP tests index unopened files, resolve imports/definitions, gather
references from multiple modules, complete qualified members and preserve
foreign signatures. Rename returns versioned open edits and version-null closed
edits, rejects duplicate/reserved names, validates all proposed sources and
bindings, and executes the resulting renamed program through the CLI. Tests
also change unsaved dependency signatures, move declarations, close overlays,
watch disk edits/create/delete and discard rapid obsolete versions.

The C11 consumer uses persistent workspace overlays, checks an artifact cache
hit and executes after releasing workspace/program handles. .NET tests compile
files, retain linked code after deleting a dependency, receive path-aware errors,
observe workspace hit/body-edit statistics, share native publication between
unchanged builds, and retain instances after workspace disposal.

## Sanitizers and bounded fuzz campaigns

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1.
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.
Generated native instructions themselves are not sanitizer-instrumented.
The previously documented noinline generated-entry wrapper suppresses only
Clang's indirect function metadata check.

| Target                                        | Requested time | Executions | Actual fuzzer duration |
| --------------------------------------------- | -------------- | ---------- | ---------------------- |
| Source                                        | 30 s           | 3,372      | 31 s                   |
| Raw bytecode                                  | 30 s           | 67,205     | 31 s                   |
| Portable container / repaired CRCs            | 30 s           | 57,010     | 31 s                   |
| Two-file workspace / cache / linked execution | 30 s           | 1,589      | 31 s                   |

All four initial campaigns exited successfully with no crash/leak/UB report.
Inputs are bounded to 2,048 / 1,024 / 16,384 / 8,192 bytes and five seconds per
input. The workspace provider is a closed two-file in-memory map; fuzzed imports
never read arbitrary files. Mutable corpora/logs stay under build/linux.
fuzz-validation06.log records the initial four campaigns. Smoke fuzzing is limited
evidence, not exhaustive validation.
An additional workspace campaign completed 1,334 executions in 31 seconds after
graph/path hardening; final-validation06.log records that run.

ThreadSanitizer execution/concurrency checks from 0.5 are recorded in that
version's report. A separate TSan build and CI job remain available for 0.6.
Root/symlink confinement is tested on Linux by the workspace-service suite.

## Artifacts and end-to-end portability

Local artifacts:

- build/mplx-0.6.0-windows-x64.zip
- build/linux/mplx-0.6.0-linux-x64.tar.gz
- build/mplx-0.6.0.vsix

Installed Windows and Linux CLIs independently build the three-file modules
example with an explicit source root. Their 2,696-byte portable artifacts are
identical. The installed Linux runtime executes the Windows-built module:
result 42, instructions 69, peak frames 4, native calls 3, one native execution,
zero fallback, and successful interpreter/native verification.
The preserved installed 0.5.0 Windows CLI also loads and natively executes this
0.6 module with the same result/counters, confirming the unchanged format-1.0
contract for this artifact.

The VSIX contains the watcher-enabled extension, server, workspace index and
runtime dependencies. Its extracted server is checked with the cross-file IPC
suite. Native packages include the public ABI header, examples and wire/
embedding/module contracts. SHA-256 values and sizes are generated by
scripts/artifact-manifest.mjs into build/artifact-manifest.json.

Windows needs the Visual C++ x64 runtime. Linux archives target the container's
glibc/libstdc++ environment. .NET uses cached SDK packs locally; the network-
disabled Linux run emitted NU1900 audit-feed warnings while its 235 checks passed.

## Repository formatting validation (2026-10-13)

The full formatting command covers 207 source and text files, including
historical optional modules, C/C++, C#, TypeScript/JavaScript, CMake,
PowerShell, MPLX examples, project/config files and documentation. It passes
`npm run format:check` after formatting. Tool versions and setup are documented
in [FORMATTING.md](FORMATTING.md), and a dedicated CI job runs the same check.

The formatted sources were rebuilt and checked locally:

- Windows MSVC Release with JIT: all 27 CTest suites passed.
- Linux Clang with JIT, ASan, UBSan and leak detection: all 27 CTest suites passed.
- Windows .NET embedding against the new Release native library: 235 checks passed.
- Strict TypeScript builds for the language server and VS Code extension passed.
- TypeScript/JavaScript AST structure and existing JSON data matched the local
  pre-format snapshot; all 16 fuzzer corpus files remained byte-for-byte unchanged.
- `git diff --check` passed.

The old `step3-apply.ps1` generator already had four invalid here-string
terminators. They were corrected before PowerShell formatting and syntax
validation. Historical generators were parsed/formatted without being executed.
Generated bundles are rebuilt from source; binaries and build/dependency outputs
are excluded from source formatting. The new GitHub formatting job has not yet
been executed remotely.

## Audit regressions (2026-10-13)

The [architecture audit](TECHNICAL_AUDIT.md) reproduced and fixed two defects:
host-locale-dependent identifier classification, and inconsistent Unicode path
keys between the native compiler and Windows LSP. The frontend suite checks the
ASCII contract in available character locales and across every high byte.
Workspace stdio/IPC suites exercise capitalized Cyrillic filenames, definitions,
open overlays, moved declarations and versioned rename.

After the fixes, Windows Release/JIT and Linux ASan/UBSan/JIT passed all 27 CTest
suites. Strict TypeScript builds passed for the server and extension. Direct
Windows C ABI and WorkspaceIndex probes confirmed the before/after behavior.
No new benchmark or .NET-specific campaign was run as part of this audit.

## Reproduction and remaining limits

```sh
cmake --preset jit
cmake --build --preset jit
ctest --preset jit
npm run format:check
sh scripts/linux/validate.sh release
sh scripts/linux/validate.sh asan
sh scripts/linux/validate.sh fuzz
sh scripts/linux/validate-dotnet.sh
```

Build the LSP dependencies first to include its suites during CMake configuration.
Windows executable suffixes and MSVC presets are described in README.md.
Linux image source is scripts/linux/Dockerfile; validation mounts source read-only
and build/linux read-write, without network after image creation.

macOS/GitHub jobs remain unexecuted. The module system rejects cycles, supports
one workspace root and has no transitive exports. Namespace/file rename,
separate object-module linking, persistent disk caches, strings/arrays and
multi-root projects remain future work. CLI processes perform one build; the
persistent native/service APIs provide incremental reuse.
