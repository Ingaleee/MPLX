# MPLX architecture

```text
mplx-lang                       syntax / AST / source spans
    ^
mplx-compiler                   resolver / types / IR / lowerer / verifier
    ^         ^
mplx-vm       mplx-jit           bounded execution / native modules
    ^              |
    +--------------+
    ^
mplx CLI / mplx-capi (.NET)      commands / external ABI
```

CMake enforces direct core-library dependencies in cmake/Architecture.cmake.
Native code consumes bytecode verification contracts and an explicit context;
it does not depend on VM layout. VerifiedModule owns immutable bytecode/facts;
CompiledProgram owns shared native publication and its RX mapping. Independent
VMs retain the program and own stacks/counters/budgets; execution owns an arena
of language frames. C callers own returned
strings until mplx_free.

The compiler service exports source-spanned symbol identities. The TypeScript
LSP owns immutable document versions and converts byte offsets into UTF-16
positions. The extension owns and disposes its LanguageClient; the server owns
and disposes its compiler process. No temporary source files participate.

WorkspaceCompiler owns canonical file loading, graph traversal and bounded
parsed/interface caches. Module checks resolve only explicit imported exports;
linking replaces signature prototypes with actual whole-program IR indexes.
Source overlays never write to disk. The LSP's root-scoped index shares compiler
facts, uses watcher/document generations and validates multi-file rename through
proposed overlays before returning versioned edits. Persistent C/.NET workspaces
reuse the compiler cache while result programs retain independent runtime lifetime.

The portable reader bounds all section metadata before allocation and invokes
verification before constructing a program. C instance handles reject concurrent
invokes on one instance; .NET SafeHandle retains native lifetime during calls.

See docs/adr for verification, typed IR, native calls, LSP, shared ownership and
portable format decisions.
The architecture/layers aggregations are historical; the concrete CMake target
graph establishes the enforced boundaries.
