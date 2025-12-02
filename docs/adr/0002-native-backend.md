# ADR 0002: a bounded x64 leaf backend

Status: accepted for the experimental 0.3 backend.
Superseded for 0.4 calls, frames and telemetry by [ADR 0004](0004-native-calls.md).

## Context

The previous backend evaluated some functions during compilation and mixed this
with incomplete VM register/ABI translations. A return of the right constant
did not establish correct native execution. Executable mappings and cached
entry pointers had incompatible owners.

## Decision

Generate machine code from independently verified bytecode. Stack-height facts
assign each operand a fixed native-frame slot; locals use separate slots.
The generated leaf function accepts an explicit ExecutionContext, decrements
the instruction budget at every bytecode instruction, records its PC, and
returns trap status to C++ rather than unwinding through generated frames.

Windows x64 and System V pass the context in their own argument register. Only
volatile registers and saved RBP are used. Frames are capped at 256 slots to avoid
stack probing; functions are capped at 64 KiB of bytecode. Calls and nonzero entry
arity are unsupported. Unsupported code falls back to the interpreter.

Arithmetic emits overflow branches. Division checks zero and MIN/-1 before IDIV.
Executable memory is allocated RW, populated, changed to RX, and freed through
platform RAII. The VM owns its code cache; the module owns no execution pointers.
Trace/cancellation run through the interpreter. Stats report actual native
executions, compilations and fallbacks, including warmup/cache behavior.

## Alternatives

An LLVM backend offers mature optimization and register allocation, at the cost
of a much larger dependency and embedding/distribution surface. Adoption should
follow typed IR and a measured need. Expanding the old partial translator would
retain unclear ownership and semantics.

## Consequences

The backend is deliberately leaf-only. It accelerates arithmetic, locals,
comparisons, booleans, branches and loops, including runtime traps. It does not
claim native recursion or cross-function tiering. Every generated-program test
requires real native execution on x64 rather than accepting a silent fallback.
Native operand peak is a verifier upper bound; interpreter peaks are observed.
