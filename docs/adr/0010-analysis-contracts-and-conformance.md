# ADR 0010: executable semantics, analysis preservation and pass replay

Status: accepted for the scalar compiler. Extends ADR 0009.

## Problem

An expanding optimizer needs reusable analyses with explicit invalidation. Structural
verification alone cannot show that a well-typed rewrite preserves the first
arithmetic fault, its source location or a callee's possible divergence. Aggregate
pass timings also cannot reproduce a failing pass without reconstructing all
earlier transformations.

## Decision

Keep the independent AST interpreter as the source-level semantic oracle. Add a
versioned, machine-readable normative table with authored outcomes, generate its
C++ corpus, and execute every intermediate optimization result against it.

Use immutable, demand-computed analyses. Compare canonical content when checking
cache identity and pass preservation claims. Start with conservative function-wide
def-use/liveness keys and module-wide effect keys. Avoid using diagnostic hashes as
correctness-critical cache identities. Unknown operations and calls remain effectful.

Propagate trap/divergence summaries through call graph SCCs. CFG cycles and
recursion imply possible divergence. Remove unused calls only when the summary
proves they are total and externally unobservable. This preserves scalar outcomes;
optimization may still change execution resource use under semantics v1.

Conservative integer intervals refine trap summaries. Arithmetic extrema must fit
the checked destination type; division excludes zero, remainder also excludes the
i64 MIN/-1 carrier case. Cyclic ranges widen after three expansions. Ranges do
not establish termination or branch predicates. The same proof table drives DCE
and function summaries; effect analysis queries cached ranges and topology.

Capture versioned canonical IR before/after each pass only when requested. Verify
the candidate and preservation claims before committing it. Replay selects the
actual registered production pass and compares complete canonical results,
including source metadata. Bound snapshot parsing, trace retention and fixed-point
analysis work separately.

## Consequences

Compiler dumps become usable inputs for external inspection and regression tools.
Generated conformance cases have an additional stale-generation check. Reusing
topology after DCE avoids rebuilding CFG for linear block merging. Effects and
dataflow invalidation are intentionally conservative; finer keys can follow measured
need. Ordinary pass reporting now pays for content fingerprints and structural diffs;
the benchmark suite measures compile latency separately from execution.

The snapshot reader is a new untrusted-input boundary and receives a dedicated
fuzzer. It is not executable module format 2.0. This decision does not introduce a
typed HIR, memory SSA, heap object types, GC, automatic failure minimization or SMT
proofs. Those remain explicit roadmap work.
