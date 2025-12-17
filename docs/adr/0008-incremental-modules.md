# ADR 0008: root-scoped module snapshots, interface keys and IR linking

Status: accepted for 0.6.

Compile modules against explicit exported signatures rather than flattening
source text. Text concatenation would destroy file-local ranges and permit
unintended private-name visibility. Whole-AST name mangling would repeat semantic
work and couple editor offsets to linker transformations.

The parser owns import/export syntax and qualified call spans. WorkspaceCompiler
owns canonical paths, immutable parsed snapshots, graph traversal and cache
lifetimes. The semantic analyzer receives typed imported prototypes with foreign
symbol identity and resolves qualified calls alongside local function symbols.
The linker replaces prototype indexes with actual whole-program IR indexes and
verifies the assembled IR before bytecode generation.

Cache correctness uses exact source content and exact imported interface keys.
Body edits cannot invalidate an unchanged interface, but relocation of a foreign
declaration refreshes navigation spans independently. Every build rereads current
sources, so deletions and overlay closure cannot resurrect cached code.
Only successful verified artifacts are published. Optimization modes are keyed
separately. Graph/source/cache bounds are explicit and tested.

C/.NET persistent workspace handles expose this compiler to hosts. Programs own
shared immutable verified code and remain executable after context destruction.
The LSP consumes schema-2 workspace facts from the same native service, applies
open-document overlays and rejects stale generations. Cross-file rename is a
compiler-validated transaction with binding preservation and versioned edits.

Cycle rejection is deliberate; supporting mutually recursive modules requires
an SCC/interface initialization contract before relaxing the rule. Binary format
1.0 stays compatible; linked dependency names carry their module identity.
Multi-root indexing, namespace/file rename and persistent on-disk caches remain
separate future contracts. MODULES.md contains the public limits and examples.
