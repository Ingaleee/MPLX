# MPLX fuzz targets

source compares optimized/raw frontend acceptance, interpreter values/traps and
native execution. bytecode constructs small malformed raw modules and executes
only verified inputs with instruction/frame/value budgets. module validates
portable containers, repairs checksums to reach inner validation, checks
canonical byte round trips and compares bounded interpreter/native outcomes,
locations and resource peaks.
workspace consumes two source files separated by NUL, with a closed in-memory
provider. It checks graph diagnostics and unchanged artifact reuse, then compares
bounded interpreter/native execution of linked modules. Seeds cover exported
calls, cycles, traps, loops and root escapes; arbitrary import names never grant
the harness filesystem access.

CTest also runs each harness over committed seeds and 200 deterministic
mutations per seed without requiring libFuzzer. module/constant.mplxb is encoded
independently by module_fixture.mjs; remaining module seeds exercise typed host
parameters, native calls/source mappings and a bounded infinite loop.

```sh
cmake --preset fuzz
cmake --build --preset fuzz
# Copy seeds into a writable scratch corpus before coverage-guided runs.
build/fuzz/bin/mplx-fuzz-module -max_total_time=30 -timeout=5 -max_len=16384 scratch/module
sh scripts/linux/validate.sh fuzz
```

Each target is compiled with Clang libFuzzer, ASan and UBSan. Smoke budgets are
limited evidence; preserve minimized failures as regressions. Mutable local
corpora/logs live under build/linux rather than modifying committed seeds.
