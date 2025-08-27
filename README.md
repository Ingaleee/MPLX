# MPLX – v0.1.1

Minimal language -> bytecode compiler, stack VM (C++20), CLI, tests, and TS-based LSP/VS Code extension.

Build
- cmake --preset dev
- cmake --build --preset dev
- ctest --test-dir build/dev -C RelWithDebInfo --output-on-failure

CLI
- Run file: `build/dev/src-cpp/tools/mplx/mplx.exe --run examples/hello.mplx`
- Check file: `build/dev/src-cpp/tools/mplx/mplx.exe --check examples/hello.mplx` (prints JSON diagnostics)