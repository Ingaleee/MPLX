# ADR 0001: verified bytecode and bounded execution

Status: accepted for the 0.3 foundation.
Type/assignment proofs are implemented by [ADR 0003](0003-typed-ir.md);
shared verification/native ownership is updated by [ADR 0006](0006-shared-program.md).

## Context

MPLX previously executed raw byte vectors with unchecked operand reads and used
the same vector for local slots and operand values. Invalid control flow or a
compiler stack leak could corrupt execution. Two interpreter loops diverged.

## Decision

A shared instruction schema specifies immediate widths and stack effects.
Verification first decodes the complete module, establishes instruction
boundaries and function regions, then validates operands (including unreachable
instructions). A worklist propagates operand heights over reachable CFG edges.
Every merge must agree on height; RET has exactly one operand; a function cannot
fall through into another function or the terminal HALT sentinel.

VM construction owns a bytecode snapshot and verifies it. The interpreter uses
separate vectors for operands, locals and frames. Arguments transfer into callee
local slots; returns restore caller bases. All locals begin as zero. Stores retain
their expression value; declaration/assignment statements explicitly POP it.

Each execution has instruction, frame and combined value limits. Cancellation
uses a caller-owned atomic flag and forces interpretation. Runs reset transient
state both on success and failure; immutable bytecode and native code can be reused.

Arithmetic uses checked signed 64-bit operations. Constant folding uses the same
operations and preserves instructions that must trap. Short-circuit operators
use branches. Semantic errors are retained even inside a constant-false branch.

## Consequences

- Verification is linear in decoded instructions and control-flow edges, with
  auxiliary memory proportional to module bytes and instruction count.
- VM snapshots trade a module copy for a simple, explicit verification lifetime.
  A future shared verified-module handle can amortize this for many hosts.
- Full type checking and definite assignment are separate future milestones.
  i32/i64/bool annotations currently share the checked i64 representation.
- Limits bound VM work/storage; they do not provide OS process isolation or a
  hard wall-clock deadline. The source frontend has size/nesting bounds.
- Two bytecode validations in compile-run (compiler and VM boundary) deliberately
  preserve independent API safety. Profiling may justify a VerifiedModule type.
