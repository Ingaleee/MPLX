# MPLX compiler language server 0.6

Node 22+, strict TypeScript and vscode-languageserver 9. Build with npm ci and
npm run build, then npm test -- <absolute-mplx-path>. Requires a 0.6 compiler for
workspace analysis; the standalone schema-1 protocol is retained.

The server supports stdio and VS Code IPC. Definitions/references, hover,
completion, signatures, semantic tokens and rename consume native compiler
symbol facts and source spans. Source byte offsets convert to UTF-16 positions.
Imports provide typed qualified completion and foreign definitions; clicking an
import path opens its file. Workspace symbols include closed sources.

The first workspace folder is the root. Open immutable text overrides disk.
Changes/close, file watcher events and configuration reset invalidate editor
generations; stale work cannot publish diagnostics. The compiler's parsed/
semantic caches remain fine-grained inside its persistent process.

Cross-file rename collects stable path/function identities, builds proposed
overlays, compiles indexed projects and verifies binding preservation before
returning versioned document edits. Closed edited files are checked against disk.
Workspace diagnostics, duplicate/captured symbols and stale generations reject
the transaction. Namespace aliases and filenames are not rename targets.

The service accepts bounded ANALYZE and WORKSPACE frames. Sources: 1 MiB/file,
16 MiB aggregate, 256 files; dependency depth 64. Queue: 64 requests/16 MiB;
responses: 32 MiB; requests: 15 seconds. Failures reject pending requests and a
later request starts a fresh compiler. In-flight native analysis is not preempted.
Directory scan limits and the exact protocol are described in docs/MODULES.md.

```sh
npm run build
node tests/protocol.mjs ../../build/jit/bin/mplx
node tests/workspace-protocol.mjs ../../build/jit/bin/mplx --ipc
node tests/workspace-client.mjs ../../build/jit/bin/mplx
npm --prefix ../.. run format:check
```

The protocol suites exercise Unicode/CRLF, lexical capture, malformed framing,
closed dependencies, file create/delete, unsaved signature changes, transactional
rename, watcher refresh and stale-response rejection.
