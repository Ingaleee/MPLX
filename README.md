# MPLX вЂ“ v0.1.1

- C++20 core: lexer/parser -> bytecode compiler -> stack VM
- Tools: `mplx` (CLI), `mplx-net` (binary framed echo server), `mplx-pkg` (simple package manifest/lock)
- LSP/VSCode: TS-based server & extension

## Build
cmake --preset dev
cmake --build --preset dev
ctest --test-dir build/dev -C RelWithDebInfo --output-on-failure