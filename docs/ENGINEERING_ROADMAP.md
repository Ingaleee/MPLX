# MPLX engineering roadmap

The objective is a language toolchain whose contracts are inspectable and whose
claims have reproducible evidence. Acceptance criteria guide scope.

The user-authorized [ultimate roadmap](vision/engineering-roadmap.md) adds M01–M50
and research extensions M51–M65. Its machine-readable milestone graph distinguishes
existing baselines from full completion and keeps all acceptance criteria visible.
The current foundation package adds formal scalar conformance, reusable analyses,
effect/range proofs, snapshots, structural diffs and isolated pass replay; see
[analysis contracts](compiler/analysis-and-replay.md).

The [2026-10-13 architecture audit](TECHNICAL_AUDIT.md) maps the current
implementation to the next substantial compiler, runtime, workspace and editor
milestones, with concrete findings and acceptance criteria.

## Completed foundation (0.3 -> 0.4)

- Independent bytecode decoder/CFG stack verifier; immutable execution modules.
- Checked arithmetic; separate frames/locals/operands; execution budgets and reuse.
- A bounded versioned C ABI, .NET interop tests and actual file-based benchmarks.
- Source-spanned AST, semantic resolver/type checker and compiler symbol identities.
- Typed CFG IR with signature/type/dominance/definite-assignment verification.
- Trap-preserving IR optimization and liveness/interference-based temporary slots.
- Whole-module x64 execution with calls, recursion and explicit arena frames.
- Persistent compiler analysis, semantic LSP, runnable VS Code extension and
  process-level editor tests.
- Windows and Linux validation; sanitizer/fuzz and cross-platform CI definitions.
  Exact executed results remain in VALIDATION.md.

## Completed in 0.5: portable artifacts and embedding contexts

Versioned canonical container with bounded lengths, CRC32C, explicit endianness,
typed export signatures, source mappings and a documented compatibility policy.
Shared VerifiedModule/CompiledProgram with once-only native publication.
C program/instance handles and .NET SafeHandle ownership for repeated/concurrent
use, typed host arguments, cancellation latch and per-invocation telemetry.
CLI build/inspect/run-module, C11 smoke tests, independent wire fixtures and
module fuzzing that repairs checksums before exercising the inner validator.

Acceptance: corrupt/truncated/incompatible files fail before payload allocation/execution;
round trips preserve source errors; interleaved host contexts remain independent;
an installed artifact runs a bounded example. Actual validation and remaining
platform limits are recorded in VALIDATION.md.

## Completed in 0.6: modules and workspace semantics

Explicit imports, export visibility, qualified calls and deliberate cycle
rejection. Canonical root-confined loading, immutable parsed snapshots, exact
interface-based semantic caching and independently verified whole-program IR
linking. Persistent C/.NET workspaces accept overlays and expose real build
statistics; immutable result programs survive context disposal.

Filesystem LSP index, cross-file identities/definitions/references/signatures,
qualified completion and compiler-validated rename transactions. Watcher/overlay
generations reject stale work. Multi-module fuzzing and stdio/IPC integration
tests cover the complete path; MODULES.md defines limits and compatibility.

Acceptance: import cycles and duplicate exports have precise diagnostics;
cross-file definition/references/rename preserve bindings; changed modules
invalidate dependents and stale work cannot publish results.

Remaining module work: explicit manifests/multi-root dependency resolution,
namespace/file rename, SCC semantics for cyclic imports, separate object-module
linking and persistent cache artifacts. Add these only with concrete invalidation
and compatibility contracts.

## 3. Native optimization driven by evidence

Implemented in the post-0.6 working tree: pruned SSA, edge-aware phi verification
and parallel-copy lowering; seven verified passes; an independent AST oracle;
block-local machine values, R8-R11 allocation and copy coalescing; IR mutation
fuzzing and interleaved compiler/native benchmarks. See
[COMPILER_PIPELINE.md](COMPILER_PIPELINE.md) for the exact current scope.

Measure leaf loops, many small calls, recursion and compilation separately.
Add copy propagation, CFG simplification and CPU register allocation with
verified spill lifetimes. Evaluate LLVM against the custom backend in an ADR.
Add tier transitions only after a concrete workload establishes value.

Acceptance: identical values/traps/source locations across modes; budget
accounting remains documented; every benchmark reports actual backend use and
compilation cost. No performance claim from a single synthetic workload.

## 4. Richer language with an ownership contract

Define strings/arrays and lifetime semantics, then implement a bounded heap and
host capabilities. Decide whether tail-call semantics include resource counters.
Consider an interpreter reference model independent of bytecode lowering.

Acceptance: allocation limits and cancellation are deterministic; object lifetime
and failure cleanup survive fuzzing/sanitizers; language additions have positive
and negative conformance tests before editor/native support claims.

## 5. Continuous engineering evidence

Extend coverage-guided source/IR/bytecode fuzzing; preserve minimized failures
as regressions. Add longer scheduled campaigns, performance histories and a
compatibility matrix. Decide whether historical network/ORM/package experiments
serve an actual language workflow before maintaining them.

Acceptance: clean checkout commands reproduce tests and examples; claims link to
test/benchmark artifacts; unresolved platforms/limits are explicitly recorded.

## Working policy

Finish coherent milestones. Keep the core dependency-free and experimental JIT
opt-in. Do not run historical source-generating bootstrap scripts as a build.
A larger feature list is not evidence of stronger architecture; each addition
needs ownership, invariants, failure cases and reproducible validation.
