# MPLX architecture audit: the next substantial milestones

Date: 2026-10-13. Scope: the local 0.6 working tree, including compiler,
runtime, native backend, module/workspace system, editor service, embedding,
tests and repository entry points. This is a source and architecture review.
Two focused Windows probes reproduced correctness defects, which were fixed
with regression coverage during this audit. Windows Release/JIT and Linux
ASan/UBSan/JIT were rebuilt and tested after the fixes. Other execution evidence
comes from the recorded validation runs. No new performance results were measured.

Implementation follow-up: the post-audit working tree now includes pruned SSA,
seven verified passes, independent AST execution, block-local native register
allocation/copy coalescing, IR fuzzing and compiler benchmarks. The root solution
now references the supported .NET binding, sample and interop checks. Findings
below preserve the audited starting point; remaining scope and current contracts
are tracked in [COMPILER_PIPELINE.md](COMPILER_PIPELINE.md) and
[ENGINEERING_ROADMAP.md](ENGINEERING_ROADMAP.md). Actual follow-up validation and
measurements are recorded separately in VALIDATION.md.

## Assessment

MPLX has a substantial compiler/runtime foundation. Verified boundaries,
immutable programs, explicit execution ownership, native language calls,
portable artifacts and compiler-backed refactoring are implemented mechanisms.
They deserve a detailed review rather than being treated as feature names.

There is considerable remaining scope. The language currently has three scalar
types; optimization and native instruction selection are limited; incremental
reuse is mainly module-level and in memory; editor invalidation is coarse.
The project can become materially stronger through coherent compiler and
runtime milestones. Additional directories or unrelated integrations would
provide much less evidence of engineering skill.

## What is already strong

- The typed IR verifier checks operand/signature types, unique definitions,
  dominance and definite assignment. Its work has an explicit complexity budget.
- The independent bytecode verifier validates decoding, instruction boundaries,
  function regions, control-flow targets and operand-stack joins.
- Immutable `VerifiedModule` and `CompiledProgram` separate verified code from
  invocation state; native publication is synchronized with `std::call_once`.
- Language recursion uses explicit native arena frames and continuations instead
  of recursive host machine frames. Windows and System V call conventions are
  handled separately; executable mappings transition from writable to executable.
- Module compilation preserves file identities and exported interfaces rather
  than concatenating text. Workspace caches distinguish source and interface changes.
- Cross-file rename compiles proposed overlays, compares bindings and checks
  document/disk versions before returning edits.
- Corrupt-container fixtures, checksum-repaired fuzzing, differential programs,
  C11 consumers and .NET lifetime/concurrency checks provide useful evidence.

See [VALIDATION.md](VALIDATION.md) for exact executed platforms and limitations.
Twenty-seven CTest entries are suites with internal checks, not a count of every
individual test case. Passing suites do not establish exhaustive correctness.

## Reproduced correctness defects fixed during the audit

### A01: host locale changed the accepted identifier language

**Priority:** P1 correctness. **Status:** fixed locally, with regression tests.

[Lexer](../Domain/mplx-lang/lexer.cpp) used locale-sensitive C character
classification despite the documented ASCII identifier contract. The same UTF-8
source, `fn я()->i64{return 1;}fn main()->i64{return я();}`, produced 21 syntax
diagnostics through the Windows C ABI in locale `C`, and none after the host
selected `Russian_Russia.1251`. Both calls successfully returned analysis data;
their diagnostic content, rather than the API return status, exposed the defect.

The lexer now uses explicit ASCII alphabetic/digit predicates. The same API probe
now reports the same 21 diagnostics in both locales. The frontend suite switches
through available locales, checks every byte from 128 through 255, rejects
non-ASCII names and still executes valid ASCII code with UTF-8 comments. A guard
restores the process's previous character locale after the test.

### A02: Unicode case folding broke Windows editor file identity

**Priority:** P1 editor correctness. **Status:** fixed locally, with regression tests.

The native compiler's path key folds ASCII letters; `WorkspaceIndex.path` used
JavaScript's Unicode `toLowerCase`. For the existing file `Математика.mplx`,
native workspace analysis returned `Математика.mplx`, while the editor requested
`математика.mplx`. A real `WorkspaceIndex.snapshot` call returned null.

The editor now preserves the canonical Unicode spelling from `realpath` and
matches the compiler's ASCII-only folding. The repeated probe returns an equal
key and a non-null snapshot. Both stdio and IPC workspace tests now cover a
capitalized Cyrillic filename, cross-file definition, opening the dependency,
an unsaved overlay that moves its declaration, and versioned transactional rename.

The before/after probe records remain in ignored `build/audit-locale*.json` and
`build/audit-path*.json`; the reproducible regressions are in
[runtime_tests.cpp](../Presentation/tests-cpp/runtime_tests.cpp) and
[workspace-protocol.mjs](../Presentation/Mplx.Lsp/tests/workspace-protocol.mjs).

## Findings and expansion opportunities

### 1. Repository entry points need consolidation

**Observed:** [Mplx.sln](../Mplx.sln) references
`src/Mplx.Compiler/Mplx.Compiler.csproj` and
`src/Mplx.Language/Mplx.Language.csproj`; both paths are absent. Several C# layer
projects contain empty `Class1` classes. Git still tracks 84 paths matching
binary, `obj` or `dist` output patterns. The historical
[build-simple-dll.ps1](../scripts/build-simple-dll.ps1) generates an implementation
that returns 42 and can create a dummy DLL. The supported core uses real native
code and does not depend on that script, as its documentation explains.

**Effect:** a reviewer can enter through a broken solution or encounter placeholders
before reaching the maintained implementation. Generated files obscure review.

**Next work:** establish one supported native/editor/embedding layout; correct or
retire the obsolete solution; archive historical generators and placeholders;
remove generated outputs from tracking while retaining distributable releases
as release artifacts. Maintain one verified clean-checkout quickstart.

**Acceptance:** every advertised entry point builds from a clean checkout, packaged
embedding examples use the freshly built library, and generated output does not
appear in source changes.

### 2. Promote mutable locals to SSA and introduce a pass manager

**Observed:** [ir.hpp](../Application/mplx-compiler/ir.hpp) has `Load` and `Store`,
without phi nodes or block arguments. Virtual values already have unique
definitions and dominance checks; mutable locals and short-circuit joins still
use slots. [optimize_ir](../Application/mplx-compiler/ir.cpp) performs local
constant folding, CFG reachability removal and removal of unused total values.
There is no distinct analysis/pass manager or general SSA optimization pipeline.
This is explicitly described in [ADR 0003](adr/0003-typed-ir.md).

**Next work:** local promotion, phi construction/verification/elimination,
analysis invalidation, sparse conditional constant propagation, copy propagation,
common-subexpression elimination and CFG simplification. Introduce an effect
contract before code motion or inlining: checked arithmetic and narrowing can trap.
For example, rewriting `(x + 1) - 1` to `x` changes behavior when the addition overflows.

**Acceptance:** verify before/after each pass; exercise loop and branch joins,
critical edges and phi-copy cycles; compare values and error locations with the
reference semantics. Demonstrate a reduced instruction count on representative
programs without claiming a speedup until measured.

[LLVM's pass documentation](https://llvm.org/docs/Passes.html#mem2reg-promote-memory-to-register)
provides a reference for SSA promotion and related transformations. This does not
require adopting LLVM as a dependency.

### 3. Separate machine IR and allocate CPU registers

**Observed:** [JitCompiler](../Application/Jit/jit_compiler.cpp) translates verified
bytecode directly into x64 bytes. [X64Emitter](../Application/Jit/x64_emitter.hpp)
loads/stores values in slots addressed through the active arena pointer.
[allocateTemps](../Application/mplx-compiler/compiler.cpp) colors an interference
graph to reuse bytecode slots; that is not CPU register allocation.

**Next work:** a target-specific machine IR with instruction constraints,
register liveness, an initially simple linear-scan allocator, spill/reload
verification, parallel-copy resolution, call clobber handling and explicit
allocation/emission diagnostics. Preserve the current backend as a comparison
path while establishing the new backend's correctness.

**Acceptance:** force register pressure and spills; cover division constraints,
values live across helper calls, arena relocation, Windows/System V ABI behavior,
traps and logical resource accounting. Record code size, compile cost and memory
traffic along with execution time.

The [LLVM backend guide](https://llvm.org/docs/WritingAnLLVMBackend.html) is a
useful reference for the boundaries between selection, allocation and emission.

### 4. Make incremental reuse granular and persistent

**Observed:** [WorkspaceCompiler](../Application/mplx-compiler/workspace.cpp)
caches exact source and imported interfaces. An unchanged entry reuses its artifact.
A changed artifact reassembles the complete IR, runs optimization and lowers the
whole program again. Each build rereads sources. Caches are process-local;
there is no manifest, separate object-module format or persistent disk cache.

**Next work:** queries for parsing, exported interfaces, function resolution/type
checking, optimized bodies and lowering. Record dependencies and use stable
entity identities. Separate executable-body fingerprints from source-location
and editor-presentation data. Add deterministic object modules, relocation,
versioned cache keys and atomic cache publication before package resolution.

**Acceptance:** prove which functions are reused after body, signature, comment,
declaration-position and dependency changes; compare warm/cold builds and clean
rebuilds; reject corrupt/incompatible cache entries; serialize concurrent cache
writers safely. Add parallel scheduling only after query ownership is explicit.

### 5. Add heap values with an explicit lifetime contract

**Observed:** [ValueType](../Domain/mplx-lang/source.hpp) supports `i32`, `i64` and
`bool`; [VM](../Application/mplx-vm/vm.hpp) stores values in `long long` vectors.
The language has no strings, arrays, structs, closures or generics.

**Next work:** define a value representation and bounded heap; add strings,
arrays and structs with indexing, layout and ownership rules. Select and explain
an initial memory strategy, such as tracing collection or reference counting.
Cycles, host-held roots, allocation failures and object handles must be part of
that decision. Closures, algebraic data types and generics can follow once these
contracts work across all supported execution paths.

**Acceptance:** memory limits, bounds checks, stable handles, failure cleanup and
retained host values survive adversarial inputs and sanitizer testing. Update
verification, serialization and ABI contracts together; change format versions
when representation or instruction semantics become incompatible.

### 6. Add host capabilities and native cancellation

**Observed:** the [C ABI](../Infrastructure/mplx-capi/capi.hpp) lets the host call
MPLX exports. It does not register host functions callable from MPLX source.
Cancellable instances deliberately use interpretation; the native execution gate
in [vm.cpp](../Application/mplx-vm/vm.cpp) excludes cancellation and tracing.
Current native calls are calls between language functions, not a general foreign
function interface.

**Next work:** typed host-function registration, an explicit capability list,
callback ownership, reentrancy/thread rules, error conversion and fuel charging.
Introduce native safepoints with a documented cancellation-latency bound. Decide
which callbacks and operations can block before advertising deadline guarantees.

**Acceptance:** unauthorized calls fail, callback failures cannot unwind through
generated code, cancellation remains responsive, and interop lifetime tests cover
repeated calls, disposal, reentry policy and multiple concurrent instances.

### 7. Make editor analysis incremental and resilient to incomplete code

**Observed:** [WorkspaceIndex.invalidate](../Presentation/Mplx.Lsp/src/workspace-index.ts)
clears all projects, dependencies and snapshots. Native semantic caches survive,
but the editor side rebuilds its index. The index supports one root and at most
256 sources. Cancellation discards stale results without preempting native
analysis. The parser recovers some syntax and records symbol facts, but does not
maintain a lossless incrementally edited syntax tree.

**Next work:** precise index invalidation, shared query snapshots, obsolete-request
coalescing and cancellable analysis. Evaluate a lossless CST with error nodes and
incremental reparsing; [Tree-sitter](https://tree-sitter.github.io/tree-sitter/) is
one reference for that design, not an automatic dependency choice. Extend the
supported editor contract with code actions, inlay hints, formatting, call hierarchy,
semantic-token deltas and namespace/file rename. Multi-root support requires a
project/dependency model first.

**Acceptance:** measure request latency and retained memory under repeated edits;
test incomplete imports/signatures/expressions, Unicode ranges, cancellations,
file moves and concurrent changes. Increase source-count limits only with measured
work/memory bounds. More advertised LSP methods alone are not the acceptance test.

### 8. Build source debugging on explicit language frames

**Observed:** runtime errors carry one function, PC and span; there is no debug
session API for stepping, breakpoints, stack inspection or variable inspection.
`SourceSpan` has positions without a source-file identifier. Dependency paths
are carried in linked function identities rather than a dedicated debug file table.

**Next work:** file-aware debug metadata, a debugger session state machine,
interpreter stepping/breakpoints and scoped variable locations. Expose it through
the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/overview).
Add optimized/native debugging only after describing lost/eliminated variables,
native PC mappings and safe suspension points.

**Acceptance:** debug across imports and recursion; step into/out/over calls; inspect
shadowed locals; resume after breakpoints; preserve handles while paused; report
unavailable optimized values honestly.

### 9. Strengthen the independent correctness oracle

**Observed:** generated arithmetic, loop and call tests already compare expected
values and native/interpreted outcomes. Source fuzzing compares optimized/raw
compilation and both execution modes, which share the frontend and lowerer.
There is no independent interpreter covering the complete source language.

**Next work:** an AST reference interpreter with separately implemented evaluation
rules, typed grammar-based program generation, module-graph generators and a
failure reducer. Compare reference, raw bytecode, optimized bytecode and native
outcomes. Add verifier/IR mutation targets and long campaigns with retained corpora.

**Acceptance:** minimized failures become regressions; compare value, trap kind,
function and source location. State when resource outcomes are permitted to differ
under optimization instead of forcing an invalid equivalence. Inspect coverage of
specific passes and rare transitions, not just execution counts.

### 10. Establish current performance and release evidence

**Observed:** the two stored benchmark reports describe numeric loops/calls and
identify version 0.4.0. [benchmark.mjs](../scripts/benchmark.mjs) measures warm
`run-only` execution and verifies actual native use. It does not yet provide a
compiler/workspace/editor performance suite. Recorded fuzz campaigns are short
smoke runs. [VALIDATION.md](VALIDATION.md) records local Windows/Linux checks and
explicitly identifies unexecuted remote/macOS validation.

**Next work:** benchmark the current release with cold compile/JIT, warm execution,
incremental edits, linking/loading, LSP requests and memory measurements. Include
several workloads and scales, interleave comparison runs, retain commit/toolchain
metadata and report variability. Publish actual CI results and release artifacts
with a compatibility matrix. Extend to native ARM64, AOT or WebAssembly when a
chosen workload and portability requirement justify that backend.

**Acceptance:** results reproduce from a clean checkout; observed backend use and
compile costs accompany throughput; regression thresholds account for noise;
platform support claims correspond to executed artifacts and tests.

## Recommended sequence

1. Consolidate repository entry points and publish reproducible current baselines.
2. Establish the stronger reference oracle, then implement SSA, a pass manager
   and the machine-IR/register-allocation backend as one coherent compiler program.
3. Introduce heap values and host capabilities through a complete vertical slice
   spanning syntax, types, verification, runtime, embedding and editor support.
4. Develop query-based incremental compilation, object modules, persistent caches
   and the editor's precise invalidation using the established contracts.
5. Add debugger support and a substantial application that exercises the language,
   embedding, imports, resource limits and diagnostics together.

Stages have internal deliverables; they are not a request to land every mechanism
in one patch. Before optimizing loops or introducing tiering/deoptimization,
establish representative performance results and preserve the observable trapping
and resource model. An optional LLVM backend merits a measured ADR; keeping the
custom backend can itself provide strong evidence when its correctness and costs
are explained and tested.

A useful flagship demonstration is a real embedded rule/analysis application in
C++ or .NET: typed input, several modules, approved host functions, retained
compiled programs, incremental edits, controlled execution and source debugging.
Define a concrete workload before designing its API. Completing this workflow
would demonstrate how the components cooperate under failures and change.
