# MPLX scalar semantics v1

This document specifies the implemented scalar language. It does not claim that the
ultimate roadmap's heap types, generics, coroutines or host callbacks already exist.
Package version `0.6.0`, semantic version `1`, IR snapshot version `1`, portable
module version `1.0` and C ABI version `1` are separate contracts.

The normative rule table and its executable examples live in
[semantics.json](../../conformance/semantics.json). Run `node scripts/conformance.mjs`
after changing that table; `--check` detects stale generated C++ independently of
formatting. Every rule must have at least one case. The test harness compares
declared outcomes against the independent AST interpreter, raw VM, every intermediate
optimization result, VM JIT, and both arena and register-allocated native code.
Expected values and fault ranges are authored in the table, not copied from compiler output.

## Syntax and typing

The grammar contains function declarations, relative module imports, `export fn`,
`let`, assignment, expression statements, `return`, `if`/`else`, and `while`.
Function and local identifiers use ASCII. Comments beginning with `//` may contain
UTF-8. See [the language guide](../LANGUAGE.md) and [module rules](../MODULES.md)
for spelling and workspace behavior. A source file is at most 1 MiB and syntax
nesting is bounded at 128.

The value domains are:

| Type   | Allowed semantic values | Runtime carrier       |
| ------ | ----------------------- | --------------------- |
| `i32`  | `[-2^31, 2^31 - 1]`     | signed 64-bit integer |
| `i64`  | `[-2^63, 2^63 - 1]`     | signed 64-bit integer |
| `bool` | `false`, `true`         | exactly `0`, `1`      |

Integer literals have type `i64`. Literal magnitudes above `2^63 - 1` are rejected;
write the minimum as `(-9223372036854775807 - 1)`. An omitted parameter or function
return annotation defaults to `i32`. A local without an annotation infers its
initializer's type. `bool` is distinct from integers.

For integer operands `a: A` and `b: B`, arithmetic result width is `A` if `A = B`,
otherwise `i64`. Equality accepts two integers of the promoted width or two booleans.
Ordered comparison requires integers. Comparison and logical operators return `bool`.
Implicit conversions are allowed between integer widths; integer/boolean conversion
requires an explicit `i32(e)`, `i64(e)` or `bool(e)`. Conditions also convert through
numeric nonzero truthiness. Conversion names are reserved.

## Evaluation judgment

Write `⟨e, environment⟩ ⇓ (type, value)` for successful evaluation and
`⟨e, environment⟩ ⇑ (Arithmetic, function, span)` for a checked fault.
The environment consists of frame-local lexical scopes and resolved module namespaces.

For a binary expression, evaluate its left operand, then its right operand, then
perform the operation and any result-width check. If the first operand faults, the
second does not execute. For a call, evaluate argument `j`, convert it to parameter
`j`'s type, and only then evaluate argument `j + 1`. A narrowing fault in an early
argument precedes a division fault in a later argument.

`a && b` evaluates `b` only if `a` is true after truth conversion. `a || b`
evaluates `b` only if `a` is false. Both produce normalized booleans. Arithmetic
precedence is `||`, `&&`, equality, ordered comparison, `+ -`, `* / %`, unary
`- !`, and calls. Binary arithmetic associates left to right.

`let x = e` evaluates `e` in the previous environment, then installs `x` in the
current scope. A nested binding may shadow an outer binding; declarations in one
scope must be unique. Each selected branch and each loop body invocation has its
own scope. Assignment resolves the nearest binding. A local can hide a callable
function; imports are a separate namespace. All source is checked before
optimization, including source in an unreachable branch.

Function declarations resolve before execution, so forward calls and recursion
are allowed. Falling off a function returns typed zero, including `false`.

## Checked arithmetic

Addition, subtraction, multiplication and negation must produce a value in the
result type's domain. Division truncates toward zero; remainder has the dividend's
sign. Zero divisors fault. The 64-bit carrier rejects both `i64 MIN / -1` and
`i64 MIN % -1`. For `i32 MIN / i32(-1)`, division succeeds in the carrier and the
result-width check faults. `i32 MIN % i32(-1)` produces zero.

Implicit assignment, argument and return narrowing to `i32` checks its domain.
`bool(integer)` maps zero to false and all other integers to true.
`integer(bool)` maps false/true to zero/one. Host input carriers are validated before
execution: wrong arity, out-of-range `i32` and noncanonical `bool` are `InvalidEntry`.

Fault spans identify the full checked arithmetic or explicit conversion expression.
Implicit conversions identify the converted source expression. Parentheses do not
expand an inner expression's span. Spans use half-open UTF-8 byte offsets and
one-based line/column metadata; the LSP converts positions to UTF-16. Error function
identities follow the linked module naming convention. Bytecode PCs are backend
metadata and need not match across different optimization results.

## Observable behavior and optimization

A completed invocation observes a typed result or an arithmetic fault with its
function and source span. There are currently no language-visible heap writes,
external reads or registered host calls. Frame-local writes are internal, but a
callee's fault or nontermination remains observable even when its return value is unused.

An optimizer may remove a call only if the interprocedural summary proves it cannot
trap, cannot diverge and has no external effects or unknown call. Recursion and any
reachable CFG cycle conservatively imply possible divergence; termination is not
inferred from a loop's apparent induction pattern. Summaries propagate through the
call graph to a fixed point. Checked operations are conservatively effectful until
folded successfully or proven total by conservative integer ranges. Argument computations remain independently observable when a
total call is removed.

Resource exhaustion is an execution policy outcome, separate from arithmetic
semantics. Defaults are 10,000,000 bytecode instructions, 1,024 frames and 1,000,000
combined live local/operand values. AST execution has independent step and
continuation budgets. Optimizations may change fuel use, frame count and storage,
so limit failures need not be identical across different code versions. VM and
native execution of the **same bytecode** must agree on instruction accounting,
resource failures and semantic outcomes. In-process native code is not a process
security boundary.

Invalid IR, invalid bytecode and internal runtime state failures are implementation
or artifact validation failures, never ordinary language-level arithmetic outcomes.
Verification must precede execution and every pass publication. Snapshot parsing
enforces format bounds; IR verification then enforces typing, control flow,
dominance, phi edges and definite assignment.

## Versioning and coverage limits

A future change to checked results, ordering, conversions or observable effects
requires a semantic version decision and corresponding corpus changes. The current
portable module header does not yet carry a semantic feature table; extending it
belongs to M29. Snapshot-local indices are not stable identities across source edits.

The 52 normative cases cover scalar rules, not every supported source or every
module-path/ABI constraint. Existing workspace, verifier, differential, ABI and
fuzz suites remain required. Metamorphic tests currently generate bounded scalar
loops/calls and test alpha-renaming, insertion of an unused total call and an
unreachable trapping branch. They do not generate heap types or prove compiler correctness.
