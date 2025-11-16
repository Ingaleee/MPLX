# MPLX scripts

Build the current compiler through CMake presets in the root README.

Use `npm run format` and `npm run format:check` from the repository root for
all source languages and documentation. Setup and pinned tools are described
in [FORMATTING.md](../docs/FORMATTING.md).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
node scripts/benchmark.mjs build/jit/bin/mplx examples/native_loop.mplx build/report.json 50
```

benchmark.mjs records real CLI samples and host/compiler metadata. It requires
a JIT-enabled binary and refuses to report a native ratio when execution falls
back to the interpreter.

bootstrap-mplx.ps1, extend-mplx.ps1, step2-apply.ps1, step3-apply.ps1 and the
old DLL/wrapper scripts are historical source-generation tools. They can replace
source files and are not part of the current configure/build/test workflow.
Their presence does not imply that their generated integrations are validated.

`compiler-benchmark.mjs` records the current verified SSA/native pipeline on five
workloads, interleaves four execution modes, and attaches CPU/OS/Git and binary/
source hashes. Build with `MPLX_BUILD_BENCH=ON` and run:

```sh
node scripts/compiler-benchmark.mjs build/jit/bin/mplx-compiler-bench build/compiler-report.json 50
```
