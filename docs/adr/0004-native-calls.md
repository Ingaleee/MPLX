# ADR 0004: native module calls with explicit frames

Status: accepted for experimental 0.4 x64 backend; supersedes ADR 0002's leaf limit.

The post-0.6 backend adds machine values and block-local CPU register allocation
while preserving this frame/ABI/resource contract. See
[ADR 0009](0009-ssa-and-native-allocation.md).

Generate a whole native module from verified bytecode and stack-height facts.
One generated host entry frame saves RBP/R12/R13. ExecutionContext is an explicit
ABI shared with C++ helpers through named offsets. Windows x64 shadow space and
System V argument/alignment rules are emitted separately.

Language calls copy evaluated arguments into an arena-backed callee frame.
native_enter and native_return return native targets/continuations. Generated
code jumps between functions and continuation sites, rather than recursively
calling generated functions on the host stack. Relocation of the arena refreshes
the active frame pointer. Return restores the caller's logical locals/operand
prefix and writes its result slot. Helpers are noexcept; traps return through the
single entry frame. All indirect targets begin with ENDBR64.

Each bytecode instruction retires one budget unit and records the precise PC.
Checked arithmetic, i32 narrowing, instruction/frame/value limits and observed
stack/frame peaks agree with interpretation. Tests include factorial, Fibonacci,
mutual recursion, failures inside callees, exact budgets and 6,000 deep frames.
In 0.4 VMs own RX mappings and caches; 0.5 moves shared ownership to
CompiledProgram ([ADR 0006](0006-shared-program.md)). Bytecode contains no execution pointers.
Compilation falls back for oversized modules/frames; tracing/cancellation use
interpretation. Native calls and fallback counts are explicit telemetry.

Clang's function sanitizer assumes type metadata precedes an indirect target.
JIT mappings have no compiler-emitted metadata. Only the generated-entry wrapper
disables that one check; ASan and other UBSan checks remain enabled in runtime
helpers. Generated machine instructions themselves are not ASan-instrumented,
so verifier invariants and differential/fuzz tests remain essential.
See [Clang UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).

Alternatives: mapping CALL directly to host recursion would conflate two stack
budgets and risk host overflow. An LLVM backend would offer mature optimization
but increase toolchain/deployment dependencies. This implementation prioritizes
explicit ownership and observable execution semantics before further tiering.
