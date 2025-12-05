# Verified SSA and native allocation pipeline

This document describes the compiler milestone in the working tree after the
0.6 module/workspace foundation. The portable format remains 1.0 and the public
C ABI remains v1. Older packaged 0.6 binaries do not contain these changes.

## Boundaries and intermediate representations

Syntax parsing and semantic checking precede every optimization. The initial
typed CFG uses immutable virtual results plus mutable local slots. `--no-opt`
retains this representation. The default pipeline promotes locals to pruned SSA:
an explicit invocation block defines parameters, and phi inputs identify their
predecessor blocks. The SSA entry has no incoming CFG edge. Loop backedges enter
ordinary blocks, keeping invocation parameters outside loops.

CFG analysis computes reverse postorder, immediate dominators, a dominance tree,
dominance frontiers and constant-time dominance queries through traversal stamps.
The verifier uses this analysis instead of an all-pairs dominance matrix. Graph
walks and SSA renaming use explicit worklists, with bounded analysis work.

The IR verifier checks unique definitions, types, signatures and dominance. Phi
inputs must match the unique actual predecessors exactly; an input's definition
must dominate the end of its incoming edge. Phi nodes lead their block, and SSA
rejects mutable loads/stores. Invalid candidates fail before lowering.

## Verified transformation publication

`IRPassManager` validates its input. Each pass runs on a private candidate, which
is verified before replacing the published IR. A failed verification or thrown
transformation leaves that pass's input unchanged and identifies the failed pass.
Earlier successful passes remain published. Immutable analyses now use content
keys and checked preservation declarations. Before/after snapshots, structural
changes and isolated production-pass replay are described in
[analysis contracts](compiler/analysis-and-replay.md).

The default seven passes are:

1. Remove unreachable CFG blocks and repair phi edges.
2. Promote locals with liveness-pruned dominance-frontier phi placement.
3. Propagate sparse conditional constants using value/edge lattices.
4. Remove trivial phi values, including self inputs with one external value.
5. Eliminate common total values within dominance scopes and propagate copies.
6. Remove dead total definitions while retaining observable effects and traps.
7. Merge linear CFG regions with a unique incoming edge.

Calls, checked arithmetic and i32 narrowing remain effectful unless conservative
effect/range proofs establish totality. Recursion and reachable cycles remain
possibly divergent; unused calls to them cannot be removed.
Failed constant evaluation retains the operation and its original source span.
Optimization may change resource consumption and instruction budgets; it must
preserve completed values and arithmetic error identity/location.

`--dump-passes` reports instruction/block/local/phi counts and elapsed time,
including verification, for every pass. `reused:true` identifies a workspace
artifact hit: the recorded timings describe creation of that reused artifact.

## Phi lowering and native machine values

Bytecode lowering treats phi arguments as uses on incoming edges. Physical
temporary allocation still verifies its interference graph. Edge blocks execute
parallel copies only for the selected edge, including critical edges. All copy
sources are loaded before any destination store, preserving cyclic swaps without
introducing a new portable opcode.

The native backend first creates block-local SSA for physical frame slots.
Function entries, branch targets, fallthroughs and call continuations delimit
machine blocks. Each slot write creates a new value version; each read identifies
its version. A value entering a block has an arena backing. Calls consume their
arguments through the arena and publish their return value to the continuation.

An allocator caches these values in volatile R8-R11. Eviction chooses the
register whose nearest future use is farthest away. Equivalent copies can share
a register while retaining distinct arena backings. Overwriting one backing
removes that alias; other aliases retain their previous value. Eviction and CFG
boundaries write dirty backings to the arena. Calls/returns clear volatile caches
before helper execution and restore the active arena pointer after relocation.
RAX/RCX/RDX remain instruction-selection scratch registers, including division.

This allocation is confined to basic blocks. Cross-block global allocation,
target-independent instruction selection, inlining and deoptimization are future
work. `NativeOptions{0}` retains the arena baseline; counts 1 through 4 exercise
the allocator. Both Windows x64 and System V use the same value model with their
existing distinct helper call conventions and W^X publication.

`--dump-machine` emits values, definitions, reads/writes, block edges and static
allocation counters. Memory read/write counters count emitted arena operations,
not dynamic execution events. Register counters describe cache operations;
coalesced copies can require no emitted move. `native_bytes` records code size.

## Independent execution oracle

`mplx-reference` depends only on syntax structures in `mplx-lang`; CMake checks
this dependency boundary. It executes semantically valid ASTs using its own
scopes, import/function lookup, arithmetic/range checks and explicit call
continuations. It uses no semantic resolver, IR, bytecode, lowerer or shared
compiler/VM arithmetic helpers. Sharing the parser means it cannot independently
validate parsing. It is a testing library, not an alternate public compiler API.

The oracle has step/frame/value/pending-continuation bounds and invocation cleanup.
The continuation budget prevents wide recursive bodies from retaining an
unbounded number of statement tasks before any call returns. Early return releases
discarded tasks; limit failures and reuse reset all invocation accounting. Module tests use
a closed map of reachable sources. Argument conversion happens immediately after
each argument is evaluated, before evaluating the next. Scope, short-circuit,
checked integer and typed entry behavior are compared with raw/optimized VM and
native execution. Resource limits differ between AST and bytecode execution.

Tests include 600 generated branch/call/swap programs, 648 signed arithmetic and
comparison boundary combinations, source-error comparisons, module imports and
6000 explicit recursion frames. Separate SSA tests cover critical edges,
cyclic parallel copies and malformed phi metadata. Native tests force 0–4
registers, spills, alias overwrites, relocation, traps and identical counters.

Six fuzz targets cover source, raw bytecode, portable containers, workspaces,
mutated raw/SSA IR and canonical snapshots. Source/workspace targets compare the independent oracle;
IR mutation first verifies candidates and compares completed optimized outcomes.
Raw bytecode compares interpreter/native behavior for verifier-accepted inputs.
Bounded smoke campaigns supplement regressions; generated x64 instructions are
not themselves instrumented by ASan.

## Reproducing reports

```sh
cmake --preset jit -DMPLX_BUILD_BENCH=ON
cmake --build --preset jit
ctest --preset jit --output-on-failure
build/jit/bin/mplx --dump-ir examples/ssa.mplx
build/jit/bin/mplx --dump-passes examples/ssa.mplx
build/jit/bin/mplx --dump-machine examples/ssa.mplx
build/jit/bin/mplx --run examples/ssa.mplx --jit on --jit-verify --no-runfile
build/jit/bin/mplx-compiler-bench 30 > build/compiler-benchmark.json
node scripts/compiler-benchmark.mjs build/jit/bin/mplx-compiler-bench build/compiler-report.json 50
```

The benchmark interleaves raw VM, SSA VM, arena-native and register-native runs
across five embedded workloads. It retains samples and reports percentiles,
instruction counts, code size, static memory operations, frontend/lowering and
native compilation costs. Compilation costs are individual cold samples, not
stable distribution estimates. Three warmups per mode precede measured execution.
Every result and native instruction count is validated before a report succeeds.
The wrapper adds OS/CPU details, Git state and executable/native-source hashes.
Measured timings remain workload/platform dependent; reduced memory traffic alone
does not establish a runtime speedup. See VALIDATION.md for actual executed runs.

## Remaining flagship milestones

Heap values and ownership, host function capabilities, native cancellation,
function-level query caching, persistent object modules, precise editor
invalidation and source debugging remain substantial separate implementation
programs. TECHNICAL_AUDIT.md retains their contracts and acceptance criteria.
