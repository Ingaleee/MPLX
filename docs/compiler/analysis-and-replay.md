# Analysis contracts and reproducible optimization

The production optimizer now uses `IRAnalysisManager` for CFG, def-use and
interprocedural effects. Other analyses are available to passes and exposed by
`mplx --dump-analyses FILE`. Results are immutable. Cache identities compare
canonical content directly; diagnostic hashes cannot cause incorrect reuse.

## Available analyses

| Analysis       | Contract                                                                                                  |
| -------------- | --------------------------------------------------------------------------------------------------------- |
| CFG            | Reachability, predecessors/successors, RPO, immediate dominators, dominance frontiers and tree intervals  |
| Postdominance  | Virtual exit and immediate postdominators over finite exit-reaching paths                                 |
| Loops          | SCCs, cyclic/irreducible classification, merged natural loops, latches, exit edges, parent and depth      |
| Def-use        | Value definitions and uses, including phi predecessor identity and terminator uses                        |
| Liveness       | Live-in/out sets and edge-selected phi uses; not machine-register liveness                                |
| Effects        | Call graph SCCs, recursion, frame-local reads/writes, traps, divergence and conservative external effects |
| Integer ranges | Conservative intervals, finite widening at cyclic phi values, checked arithmetic/cast totality proofs     |

Postdominance deliberately excludes paths that never reach a return. A return can
postdominate a branch in this model while its other successor loops forever.
`reaches_exit` and `may_diverge` expose this distinction. These facts alone do not
authorize code motion, speculation or elimination of a divergent call.

Natural loops arise from edges whose targets dominate their sources. Loops sharing
a header are merged; strict set containment determines nesting. SCC entry analysis
and nondominating DFS backedges detect irreducibility, including an inner cycle
inside a single-entry outer SCC. Traversals use explicit stacks, not host recursion.
Fixed-point and potentially expensive set analyses have an 8,000,000-work budget.
These are compiler resource checks, not program instruction fuel.

Integer intervals start with declared parameter/local bounds, propagate through
casts, copies and phi joins, and prove checked operations total only when all
extreme results fit the destination type. Division additionally excludes zero;
remainder excludes zero and the i64 MIN/-1 carrier case. Ascending ranges widen
to declared type bounds after three expansions. This is conservative value analysis,
not branch-sensitive range refinement or a termination proof. For example,
`i64(x) + 1` with `x: i32` is safe; `x + 1` with `x: i64` remains potentially trapping.
Range endpoints in JSON are decimal strings to avoid JavaScript precision loss.

Phi operands are uses on incoming edges. An incoming value is live on its selected
predecessor edge, not on every edge into the phi's block. Returned values are
terminator uses. Undefined entries in the value table remain representable because
DCE preserves snapshot-local value numbering.

## Preservation and publication

A pass returns a preservation bitmask. Before publication, the manager checks
the candidate IR and compares the relevant before/after content for every claimed
analysis. A false claim aborts the pass. CFG keys include block identities and
terminator topology; changing constants or operand aliases preserves topology.
Def-use/liveness keys currently conservatively include complete function snapshots;
integer range keys do too; effects depend on the complete module. An unclaimed analysis is invalidated.
Every query also checks its content key, protecting against interim direct mutation
inside a pass. Pass reports record requests, hits, computations and invalidations.

Only the successfully verified candidate replaces the current module. A failed
pass retains the last successful module; earlier successful passes are committed.
This is a per-pass transaction, not whole-pipeline rollback. A thrown
`IRPassFailure` contains the pass name, reason and optionally bounded input/candidate
snapshots. Candidate snapshots may describe invalid IR and must not execute.

The default pipeline still has seven passes. DCE now consumes def-use facts and
transitive effect summaries, removing unused proven-total calls while retaining
traps, recursive calls, cyclic callees and unknown effects. Dominator CSE and DCE
preserve CFG facts; linear CFG merging consumes the preserved topology and then
invalidates it. Existing checked arithmetic and source fault locations remain required.

## Snapshots and replay

`.mplxir` is compiler debugging interchange, distinct from executable `.mplxb`.
All integers use canonical little-endian encoding. The eight-byte magic is
`MPLXIR\r\n`, followed by a 32-bit version (`1`) and function count. Function records
include name, symbol, return/parameter/local/value types, source span, canonical
boolean SSA flag and blocks. Blocks contain every instruction field and complete
terminator source metadata. Signed constants preserve their 64-bit bit pattern.
Counts, string lengths and enum values are checked before use; trailing data,
invalid UTF-8, invalid ranges and invalid IR are rejected. Input is at most 16 MiB.
Canonical content keys and normal pass fingerprints have a separate 256 MiB limit;
the debugging file limit does not silently restrict ordinary compilation to 16 MiB.

`fnv1a64` fingerprints are deterministic debugging labels, **not cryptographic
integrity or authenticity checks**. Cache reuse and replay compare complete canonical
content. Metadata and spans participate in snapshot equality. Snapshot-local entity
IDs are preserved exactly; no stability across source edits is promised.

```sh
# Raw input and one isolated production pass.
mplx --snapshot-ir --no-opt --out build/before.mplxir examples/ssa.mplx
mplx --replay-pass --pass promote-locals-to-ssa --out build/after.mplxir build/before.mplxir

# Full before/after snapshots, structural block changes and analysis statistics.
mplx --dump-pass-trace --out build/trace.json examples/ssa.mplx
node scripts/replay-trace.mjs build/release/bin/mplx build/trace.json
mplx --dump-analyses examples/ssa.mplx
```

Use `.exe` on Windows. Trace capture is opt-in and defaults to a 64 MiB cumulative
binary snapshot budget. JSON carries hex snapshots plus readable IR and can be
larger. The replay script validates trace chaining and fingerprints, then executes
each real production pass in isolation and compares its entire binary result.
It does not claim semantic proof: the conformance suite separately executes every
intermediate pass result across engines.

## Evidence and remaining scope

Random CFG tests compare dominators to node-removal reachability, postdominators to
finite-exit path removal, and SCCs to mutual reachability. Targeted tests cover
three nested natural loops, irreducible inner cycles, phi edge liveness, stale cache
queries, false preservation, rollback, truncated/mutated snapshots and replay.
The sixth libFuzzer harness accepts only verified snapshots, checks canonical
round trips and compares raw/optimized VM and same-bytecode JIT execution.

M03 remains open for alias, points-to, escape, branch-sensitive ranges and a richer
interprocedural framework. M04 has no heap or host-effect tokens yet. M06 remains
open for automatic failure reduction and pass-order search. Snapshot parsing and
structural verification do not establish semantic equivalence of arbitrary IR.
