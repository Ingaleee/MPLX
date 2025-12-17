# Embedding MPLX 0.6

Compile or load a program once, then create execution instances for independent
requests. A program owns an immutable VerifiedModule and lazily published native
image. An instance owns stacks, hot counters, limits, stats and cancellation.
Instances keep the program alive after its public handle has been released.

File imports use mplx_program_compile_file / MplxProgram.CompileFile. Repeated
compilation uses mplx_workspace_create/compile/destroy or MplxWorkspace, with
source overlays and cache statistics. Result programs remain valid after
workspace disposal. These additive ABI-1 entry points are specified in MODULES.md.

## C ABI

Use the installed include/mplx/mplx.h and link mplx_native. The header is tested
by an actual C11 executable, not only C++ callers.

```c
mplx_program *program = NULL;
mplx_instance *instance = NULL;
char *error = NULL;
int64_t result = 0;
const int64_t arguments[] = {21, 2, 1};
int status = mplx_program_compile((const uint8_t *)source, strlen(source),
                                  1, &program, &error);
if (status == MPLX_OK)
    status = mplx_instance_create(program, NULL, &instance, &error);
mplx_program_destroy(program);
if (status == MPLX_OK)
    status = mplx_instance_invoke(instance, "transform", arguments, 3,
                                  NULL, &result, NULL, &error);
// Check status and copy/log error before releasing it.
mplx_free(error);
mplx_instance_destroy(instance);
```

Defaults are interpretation with cancellation enabled, 10,000,000 instructions,
1,024 frames and 1,000,000 live local/operand values. Options/stats structures
require exact struct_size and ABI version 1. Set flags=0 and select MPLX_JIT_ON
or MPLX_JIT_AUTO to allow native execution. Cancellable instances interpret
every invocation, with explicit fallback telemetry when JIT was requested.

Arguments/results use int64_t carriers. Arity must match exactly; i32 arguments
must fit the signed 32-bit range; bool arguments must be 0 or 1. Arguments count
toward the live-value budget before entry. Wrong export return representations
also fail at the host boundary. Export metadata comes from mplx_program_info.

mplx_program_save returns bytes freed with mplx_buffer_free. Info/error strings
use mplx_free. Result/handle/buffer outputs are initialized on failure; stats
require a valid header before their payload is written. Successful calls return no error
string. out_error is required on APIs that provide it. Diagnostics and runtime
faults cannot unwind C++ exceptions through the ABI.

Status codes: OK=0, invalid argument=1, allocation failure=2, runtime failure=3,
source diagnostics=4, invalid module=5, busy=6, cancelled=7. Invocation runtime
faults include JSON code, numeric kind, function, PC, source range and message.
Stats are returned after runtime success or failure. Legacy source entry points
retain their signatures and default interpreter behavior.

## Concurrency and cancellation

Distinct instances may execute concurrently against the same program. Native
publication uses std::call_once; all instances retain the same immutable RX
mapping. jit_compilations is reported by the invocation that publishes it.
JIT Auto thresholds are per-instance; a published module is reused when another
instance reaches its threshold.

A concurrent invocation on one instance returns MPLX_BUSY. Cancellation and
is_running can be used from another thread. Cancel is latched until reset; reset
returns BUSY during execution. is_running is an observation, not a reservation
for a future invocation. Instance destruction, and program-handle destruction
concurrent with host API use of that handle, require external lifetime
synchronization. No host API can validate a dangling pointer.

Limits bound language instructions/frames/values. They do not impose a wall-clock
deadline or process isolation. After an invocation fails, transient execution
state is cleared and the instance can be reused; cancellation remains latched.

## .NET

MplxProgram and MplxInstance implement IDisposable with native SafeHandle
ownership. P/Invoke pins the handle's lifetime during invoke/cancel operations.
An instance remains valid after disposing the program that created it.

```csharp
using var program = MplxProgram.Compile(File.ReadAllText("examples/embedding.mplx"));
File.WriteAllBytes("embedding.mplxb", program.Save());

using var loaded = MplxProgram.Load(File.ReadAllBytes("embedding.mplxb"));
using var worker = loaded.CreateInstance(MplxJitMode.On, cancellable: false);
var answer = worker.Invoke("transform", new long[] { 21, 2, 1 },
    new MplxExecutionOptions(MaxInstructions: 100_000));
Console.WriteLine(answer.Result); // 42
Console.WriteLine(answer.Stats.Instructions);
```

Create one instance per concurrent worker. Catch MplxException for Status,
Function, ProgramCounter, SourceRange and Stats. Cancellable instances expose
Cancel, ResetCancellation and IsRunning. Disposed handles raise
ObjectDisposedException; embedded NUL, oversized sources/modules and excessive
argument counts are rejected.

The runnable sample accepts source or module, an optional entry and its arguments:

```sh
dotnet run --project Presentation/Mplx.DotNet.Sample -- examples/embedding.mplx transform 21 2 1
```

Supply MplxNativePath/MplxNativeFileName at build time as shown in the root README.
The shared library must match the host OS/architecture.
