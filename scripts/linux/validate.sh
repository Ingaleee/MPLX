#!/bin/sh
set -eu
mode="${1:-asan}"
build="build/linux/$mode"
case "$mode" in
    release) options="-DCMAKE_BUILD_TYPE=Release -DMPLX_WITH_JIT=ON" ;;
    asan) options="-DCMAKE_BUILD_TYPE=Debug -DMPLX_WITH_JIT=ON -DMPLX_SANITIZERS=ON" ;;
    tsan) options="-DCMAKE_BUILD_TYPE=Debug -DMPLX_WITH_JIT=ON -DMPLX_THREAD_SANITIZER=ON" ;;
    fuzz) options="-DCMAKE_BUILD_TYPE=Debug -DMPLX_WITH_JIT=ON -DMPLX_BUILD_FUZZERS=ON -DMPLX_BUILD_TESTS=OFF" ;;
    *) exit 2 ;;
esac
cmake -S . -B "$build" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ $options
cmake --build "$build" --parallel 2
if [ "$mode" = fuzz ]; then
    for corpus in source bytecode module workspace ir snapshot; do
        mkdir -p "$build/corpus/$corpus"
        cp Presentation/fuzz/corpus/"$corpus"/* "$build/corpus/$corpus/"
    done
    "$build/bin/mplx-fuzz-source" -max_total_time=30 -timeout=5 -max_len=2048 -artifact_prefix="$build/" "$build/corpus/source"
    "$build/bin/mplx-fuzz-bytecode" -max_total_time=30 -timeout=5 -max_len=1024 -artifact_prefix="$build/" "$build/corpus/bytecode"
    "$build/bin/mplx-fuzz-module" -max_total_time=30 -timeout=5 -max_len=16384 -artifact_prefix="$build/" "$build/corpus/module"
    "$build/bin/mplx-fuzz-workspace" -max_total_time=30 -timeout=5 -max_len=8192 -artifact_prefix="$build/" "$build/corpus/workspace"
    "$build/bin/mplx-fuzz-ir" -max_total_time=30 -timeout=5 -max_len=2048 -artifact_prefix="$build/" "$build/corpus/ir"
    "$build/bin/mplx-fuzz-snapshot" -max_total_time=30 -timeout=5 -max_len=32768 -artifact_prefix="$build/" "$build/corpus/snapshot"
elif [ "$mode" = tsan ]; then
    ctest --test-dir "$build" -R 'shared-program|embedding-contexts' --output-on-failure
else
    ctest --test-dir "$build" --output-on-failure
fi
