# ADR 0009: verified SSA, independent oracle and block-local native allocation

Status: implemented in the post-0.6 working tree. Extends ADR 0003 and ADR 0004.

Promote mutable typed locals to pruned SSA and represent phi inputs as explicit
predecessor/value pairs. Establish a synthetic invocation entry, validate
dominance at phi edges, and lower selected-edge parallel copies to the existing
portable bytecode. This preserves container-1.0 and ABI-v1 compatibility.

Publish each optimization only after verification of its private candidate.
Recompute analyses to prevent stale dominance facts. Treat checked arithmetic,
narrowing and calls as observable effects. Introduce selective analysis caching
or code motion only through explicit preservation/effect contracts.

Keep an AST execution oracle dependent only on syntax. Its independent arithmetic
and explicit continuations compare semantics before/after lowering, covering bugs
that VM/native comparisons alone would share. Its resource meter is deliberately
different; compare completed outcomes and arithmetic failures, not consumed fuel.

Introduce block-local slot SSA between verified bytecode and x64 emission. Allocate
R8-R11 by future use, coalesce equivalent copies and flush dirty arena backings at
edges/calls. Preserve the zero-register baseline and forced one-register testing.
Keeping values across blocks requires a later global allocation contract. The
native arena remains the durable state across ABI helpers and relocation.

This design improves inspectability while retaining the custom backend and
dependency-free core. It does not claim LLVM-equivalent optimization, whole-
function register allocation or universal speedup. Interleaved workloads report
code size, compile costs, instructions, static memory traffic and actual time.

See [COMPILER_PIPELINE.md](../COMPILER_PIPELINE.md) for invariants, limitations,
tests and reproducible commands.
