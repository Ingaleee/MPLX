# ADR 0003: typed control-flow IR and semantic identity

Status: accepted for 0.4.

The post-0.6 pipeline adds pruned SSA, phi verification/elimination, a verified
pass manager and an independent AST oracle. The historical rationale below
describes the 0.4 boundary; [ADR 0009](0009-ssa-and-native-allocation.md) and
[COMPILER_PIPELINE.md](../COMPILER_PIPELINE.md) describe the implemented extension.

The AST is syntax, not an execution or optimization contract. A separate resolver
assigns snapshot-local symbol IDs and records scopes, reads/writes, declarations,
call sites and byte-accurate spans. A type checker builds i32/i64/bool IR.
All semantic checks run before optimization, including unreachable source.

IR functions contain typed virtual values, local slots, basic blocks and explicit
jump/branch/return terminators. The IR verifier checks signatures, operand types,
unique definitions, dominance, valid CFG edges and definitely initialized locals.
Mutable variables use load/store; short-circuit joins use hidden local slots.
This provides an inspectable CFG without adding phi-node machinery prematurely.

Optimization folds checked constants and removes dead total values/blocks. Calls,
narrowing checks and arithmetic traps cannot disappear merely because a result
is unused. The lowerer analyzes CFG liveness and colors the value interference
graph to reuse bytecode temporary slots. It verifies conflicting values have
different physical slots. The bytecode verifier remains an independent boundary.

--dump-ir exposes versioned JSON. Analysis work is bounded; limits reject overly
complex modules instead of allowing uncontrolled verifier growth. The reference
interpreter and generated differential tests compare optimized/unoptimized
values and traps. Resource consumption may differ.

Alternatives: direct AST optimization mixes name resolution with transformations.
A full SSA system with phi elimination and LLVM would add substantial complexity
and distribution cost before there is measured need. Native CPU register
allocation and richer optimizations remain future work; slot coloring is not
CPU register allocation.
