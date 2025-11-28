# ADR 0006: shared verified programs and isolated execution instances

Status: accepted for 0.5; updates runtime/native ownership in ADRs 0001 and 0004.

The previous VM constructor copied/verified bytecode and owned its JIT cache.
Creating a VM per host request repeated verification and native compilation,
while retaining one VM conflated reusable code with mutable request state.

VerifiedModule now owns const bytecode and its const verification result.
CompiledProgram retains that module and a native mapping published with
std::call_once. Its factory prevents publishing null or unverified modules.
VMs reference shared immutable state and retain its lifetime; each VM owns all
mutable execution data. Construction from raw Bytecode remains available and
passes through the same verification boundary.

JIT lowering consumes the verified module's existing decoded instructions and
stack heights. Only the winning invocation counts a compilation. Unsupported
native shapes are cached as unsupported; allocation exceptions permit a later
call_once retry. Interpreter/native execution contexts remain independent.
No global cache, content-hash collision policy or eviction mechanism is needed.

C programs/instances are opaque handles. Instance invocation uses an atomic lease
to reject concurrent mutation; independent instances can run in parallel.
Cancellation is a separate atomic latch, reset only while the instance is idle.
C++ callers still synchronize a single VM themselves. C callers synchronize
handle lifetime; .NET wraps handles in SafeHandle, including native outputs.

Typed export metadata survives lowering/serialization. Hosts pass i64 carriers
with checked i32/bool representations; root native entry copies parameters into
the existing explicit frame arena. Resource statistics count host arguments.
This extends the existing native calling convention rather than inventing a
second stack model.

Evidence: eight simultaneous VMs execute 800 calls with one native compilation;
C/.NET retain instances after program disposal; cancellation races with a live
invocation; busy/reset/error/reuse behavior is tested. A separate TSan build is
provided; actual executed platform results are recorded in VALIDATION.md.
