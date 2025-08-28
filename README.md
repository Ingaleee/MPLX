# MPLX — v0.2.0

- C++20 core: lexer/parser → bytecode compiler → stack VM
- Tools: `mplx` (CLI), `mplx-net` (binary framed echo/health), `mplx-pkg`
- LSP/VS Code: TS-based server & extension

## Build
cmake --preset dev
cmake --build --preset dev
ctest --test-dir build/dev -C RelWithDebInfo --output-on-failure

## Usage

### CLI
```
mplx --run <file>
mplx --check <file>
mplx --symbols <file>
mplx --bench <file> [--mode compile-run|run-only] [--runs N] [--json]
mplx --help
mplx --version
```

### Net
```
mplx-net --serve <host> <port>
mplx-netclient --send <host> <port> <hexpayload>
mplx-netclient --ping <host> <port>
mplx-netclient --health <host> <port>
```

## ROADMAP (high-level)

- LSP workspace-wide index: cross-file references/rename
- IR/CFG for better peephole, const-prop and DCE
- `mplx dump` tool for bytecode/CFG inspection
- Prometheus metrics for Net server
- Docs: compiler internals, VM opcodes, LSP protocol

## Demo script (6–8 min)

1) CLI: `mplx --check examples/hello.mplx` → JSON diagnostics
2) IDE: go-to-def, references, rename; hover/signature
3) Bench: `mplx --bench examples/hello.mplx --json`
4) Net: `--serve` + client `--ping|--health`
5) .NET sample (P/Invoke)
6) Tests: `ctest` summary