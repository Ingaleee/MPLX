# ADR 0005: compiler-backed LSP with version ownership

Status: accepted for 0.4.

LSP JSON-RPC belongs to the TypeScript server. A persistent C++ compiler child
consumes bounded length-framed UTF-8 sources and emits analysis JSON. Neither
shell commands nor temporary source files participate in analysis.

Each analysis belongs to an immutable document version and compiler configuration
epoch. Close, edit or configuration change invalidates older responses before
they can publish diagnostics. Source byte offsets are translated to UTF-16 LSP
positions, including CRLF and supplementary Unicode in comments.
Process failures/timeouts reject pending requests and allow subsequent restart.

Definitions/references/rename use compiler symbol identities, never text search.
Rename re-resolves affected names to reject capture, duplicates and changed
bindings; edits carry document versions. Completion chooses visible bindings
with lexical shadowing. Signature help uses compiler call-site spans even after
parser recovery. Symbols and semantic tokens use the same snapshot.

Process-level tests exercise diagnostics, every supported navigation/refactoring
feature, shadowing, initializer scope, malformed calls, comments, Unicode,
rapid edits and closing documents. Cancellation discards LSP results; it does not
preempt an already running compiler pass. Workspace symbols index open documents;
cross-file linking awaits language module semantics.
