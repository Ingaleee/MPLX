# MPLX 0.6 language contract

```mplx
fn sum(n: i64) -> i64 {
  let i: i64 = 0;
  let result = 0;
  while (i <= n) {
    result = result + i;
    i = i + 1;
  }
  return result;
}
fn main() -> i64 { return sum(100); }
```

The default entry is main; host arguments must match its typed signature. Functions can call later
declarations and recurse. Arguments evaluate left to right. An omitted function
return or parameter annotation defaults to i32. A missing explicit
return yields typed zero, including false for bool. This compatibility rule means
the language does not enforce explicit returns on every path.

## Types and conversions

Numeric literals have type i64; true/false have type bool. A let without an
annotation infers its initializer's type. Supported annotations are i32, i64 and
bool. Integers use checked signed arithmetic at their declared width. Mixed
integer operations promote to i64. Assignment, argument and return conversions
between integer widths are implicit; narrowing to i32 checks the range and traps.
bool is distinct: arithmetic and integer/bool equality mixing are rejected.

Explicit conversions i32(expr), i64(expr), bool(expr) accept one argument.
bool(integer) normalizes nonzero to true; integer(bool) yields zero or one.
Those conversion names cannot be function names. if/while conditions and logical
operators accept numeric truthiness. Comparisons and !, &&, || produce bool.
All runtime slots use a 64-bit representation; type rules and i32 range checks
are established by the typed IR and preserved in bytecode/native execution.

Overflow and division/remainder by zero trap. i64 MIN/-1 division/remainder
trap; i32 MIN/-1 division fails its result range check, while i32 remainder
is zero because the implementation evaluates integers in a checked i64 carrier.
Division truncates toward zero; remainder follows the dividend's sign.
Write i64 minimum as (-9223372036854775807 - 1). Larger unsigned-magnitude
literals are rejected.

## Scope and evaluation

Function bodies, if branches and while bodies establish lexical scopes.
Nested shadowing is allowed. A let initializer resolves before its new binding
exists. Assignment and calls resolve the nearest visible binding; a local can
therefore hide a function. Branch locals cannot escape their block.
Declarations in one scope must be unique. Compiler symbols are snapshot-local
identities, distinct from their spellings.

Precedence from low to high: ||, &&, == !=, < <= > >=, + -, * / %, unary - !,
function call. Logical operators short-circuit. Semantic errors in unreachable
source are still reported before optimization. Optimization can remove total
unused values; calls and operations that may trap or diverge remain observable.
Conservative effect/range proofs can establish that a checked operation or call
is total. See [scalar semantics v1](language/specification.md) for the normative
rules and executable conformance table.

Statements: let, assignment, expression statement, return, if/else, while.
Comments begin with //. Source identifiers are ASCII; UTF-8 in comments is
supported and editor positions are converted to UTF-16. Strings, arrays,
standalone blocks, else-if, assignment expressions, break/continue and for are
not supported.

Source files may import relative modules with import "math.mplx" as math; and
call exported functions as math::sum(...). export fn controls cross-module
visibility. Import namespaces are distinct from lexical value bindings; imports
are not transitive exports. Cycles are rejected with a dependency chain.
See MODULES.md for workspace roots, overlays, linking and incremental semantics.

## Limits and diagnostics

Sources are limited to 1 MiB; syntax nesting to 128. IR verification has a
bounded analysis work estimate. Default execution budgets are 10,000,000
instructions, 1,024 frames and 1,000,000 combined live local/operand values.
Optimization and slot reuse can change resource consumption; resource-limit
outcomes are not invariant under optimization.

Diagnostics carry stable codes and byte ranges with line/column metadata.
Runtime errors include function, bytecode PC and source span when bytecode was
produced by the compiler. Host-created bytecode may omit source mapping.

Host entry functions may have up to 255 typed parameters. i32 arguments must fit
their range; bool carriers must be 0/1; return carriers must match the export
type. Sources and portable modules expose the same entry convention through
CLI --arg, C instance handles and .NET MplxInstance.Invoke.
