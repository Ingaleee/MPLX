# MPLX 0.6 modules and incremental compilation

## Language contract

```mplx
import "math.mplx" as math;

export fn process(value: i64) -> i64 {
    return math::multiply(value, 2);
}
```

Imports are top-level declarations with a quoted relative path, an explicit
namespace and a semicolon. Paths use '/' separators and no escape sequences.
Namespaces resolve qualified function calls; they are separate from local-value
names. Exactly one namespace qualifier is supported. An imported namespace
exposes only functions declared with export fn. Local functions remain callable
inside their defining module. Functions may recurse inside a module.

Imports resolve relative to the importing file, inside an explicit workspace
root. Canonicalization rejects absolute imports, root traversal and symlinks
whose resolved target escapes the root. Duplicate/reserved namespace names and
duplicate function declarations are errors. Imports are not transitive exports.
An import cycle fails with the complete chain and the offending path span;
mutual recursion across module cycles is not supported.

The root file's functions keep their host entry names; dependency function names
are root-relative path::function. Dependent traps therefore retain their module
identity, bytecode PC and file-local source span. A linked portable module
contains all code and runs after dependency sources have been removed.
Format 1.0 remains unchanged; dependency paths are carried by qualified function
names rather than a new file-table section.
export governs source import visibility. The host ABI retains the earlier
convention in which any linked function can be selected by its host name;
export is not a host capability/security boundary.

Standalone string compilation has no filesystem capability and reports MPLX4000
for imports. Use WorkspaceCompiler, CLI file compilation, CompileFile or
MplxWorkspace when dependencies are needed.

## Compiler pipeline and cache

WorkspaceCompiler owns a bounded cache, not mutable execution state. Each build
reads current sources, applies explicit immutable overlays, creates/retains
parsed module snapshots, visits dependencies and checks each module against its
direct export interfaces. Linking copies real IR functions and resolves local/
imported calls to whole-program indexes. Synthetic signature prototypes are
used only during individual type checking and never reach execution.

Exact source equality determines syntax reuse. Semantic keys include the parsed
revision and length-delimited imported names/types/parameter names/defining paths.
Keys do not depend on probabilistic hash equality. Foreign declaration spans are
refreshed from current snapshots separately: moving an export must update
navigation without forcing caller type checking.

A body-only edit rechecks that module, reuses unchanged caller semantics and
relinks the artifact. An export signature edit rechecks direct consumers.
A consumer's own unchanged export interface lets higher-level consumers reuse
their semantic results; any diagnostic prevents artifact publication.
Deleted files cannot be restored from a stale cache. Unchanged builds return the
same VerifiedModule. Optimization mode is part of the assembled-artifact key.

Statistics expose parsed, syntax_hits, checked, semantic_hits, affected,
source_bytes, linked_functions and artifact_hit. They describe real operations,
not estimated cache effectiveness. They do not establish a performance claim.

| Bound                                       | Limit                     |
| ------------------------------------------- | ------------------------- |
| One source                                  | 1 MiB                     |
| Reachable files / overlays                  | 256                       |
| Total source / overlay bytes                | 16 MiB                    |
| Dependency depth                            | 64                        |
| Dependency edges                            | 4,096                     |
| Real functions / expanded imported bindings | 65,536 each               |
| Expanded imported names/signature metadata  | 16 MiB                    |
| Cached source snapshots                     | 512 / 32 MiB source bytes |
| Assembled entry artifacts                   | 8                         |
| Compiler response                           | 32 MiB                    |
| LSP queue                                   | 64 requests / 16 MiB      |
| LSP request timeout                         | 15 s                      |

Budgets bound inputs/work; they do not promise a fixed wall-clock compile deadline
or OS process isolation. C++ WorkspaceCompiler callers synchronize a shared
compiler. C workspace handles reject concurrent compilations with MPLX_BUSY.

## CLI

```sh
mplx --workspace examples/modules/main.mplx
mplx --run examples/modules/main.mplx --jit on --jit-verify --no-runfile
mplx --build examples/modules/main.mplx --out build/pipeline.mplxb
mplx --run-module build/pipeline.mplxb --jit on --jit-verify --no-runfile
```

The example returns 42 through three modules and a private implementation
function. File commands automatically compile imports; --root PATH selects a
root other than the current directory. --workspace returns schema-2 JSON with
files, symbol models, graph edges, path-aware diagnostics and cache statistics.
One CLI process performs one build; reuse the service/API for incremental work.
--build refuses to overwrite any source in its dependency graph.

## Persistent service and host API

The existing ANALYZE frame remains supported. A WORKSPACE header contains
request id, root byte length, entry byte length and overlay count, followed by
exact root/entry UTF-8 bytes. Each overlay has a FILE header with path/source
byte lengths and exact payload bytes. Header fields, counts and aggregate
payloads are bounded before allocation. Responses are JSON lines with id/result.
Header lines are limited to 256 bytes and must end with a newline.
Schema 2 contains workspace facts; nested per-file analysis remains schema 1.
The child retains one root-scoped compiler across requests.

C: mplx_workspace_create, mplx_workspace_compile, mplx_workspace_destroy.
Overlay records carry UTF-8 path, explicit source pointer and byte length.
Return programs are independent of workspace lifetime; summary/error strings
use mplx_free. Statistics can be returned on source failure too. Existing
program/instance ABI version 1 remains additive.

```csharp
using var workspace = new MplxWorkspace(projectRoot);
using var first = workspace.Compile("main.mplx");
using var unchanged = workspace.Compile("main.mplx");
Console.WriteLine(unchanged.Statistics.ArtifactHit);

using var edited = workspace.Compile("main.mplx",
    new Dictionary<string, string> {
        ["math.mplx"] = "export fn multiply(a:i64,b:i32)->i64{return a*i64(b);}"
    });
using var instance = edited.Program.CreateInstance(MplxJitMode.On, cancellable: false);
Console.WriteLine(instance.Invoke().Result);
```

SafeHandle retains workspace lifetime during calls. Each overlay's UTF-8 buffer
is pinned only for the native call and released in finally. Live programs from
an unchanged entry share native publication; the host wrapper keeps bounded
weak references rather than extending executable lifetime globally.

## Workspace LSP

Definitions, references, hover, signatures and qualified completion use compiler
facts. Import path definitions open their target file. Workspace symbols scan
closed files as well as open documents, without indexing dependency aliases as
duplicate declarations. The first workspace folder is the compilation root;
documents outside it/untitled documents keep standalone analysis.

Open text overrides disk. Document edits/close, file watcher events and compiler
configuration changes invalidate the workspace generation. Obsolete generations
cannot publish diagnostics or produce rename edits. The native compiler retains
its finer-grained semantic cache across these editor invalidations.

Function rename collects references through path + original function identity.
Local identities include their defining file. It constructs all proposed edits,
recompiles affected indexed projects using overlays and compares occurrence
bindings, then checks closed edited files still match disk. Open edits carry
document versions; closed edits carry version null. Invalid names, diagnostics,
duplicate declarations, captures and changes during validation are rejected.
Namespace alias/file rename is not implemented.

Indexing skips symlinks and build/node/generated directories. Directory scans
are bounded to depth 32, 16,384 entries, 256 sources and 16 MiB source bytes.
The index refreshes on watched create/change/delete events. Actual compiler
work already running is not preempted; cancellation/stale generations discard
the result. The server supports one workspace root, not a multi-root project
dependency system. Rename requires clean indexed workspace diagnostics.
