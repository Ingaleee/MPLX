add_executable(mplx-bench bench_vm.cpp)
add_executable(mplx-compiler-bench compiler_bench.cpp)
target_link_libraries(mplx-compiler-bench PRIVATE mplx-vm)
target_compile_definitions(
    mplx-compiler-bench PRIVATE MPLX_VERSION="${PROJECT_VERSION}" MPLX_BUILD_CONFIGURATION="$<CONFIG>"
)

target_include_directories(
    mplx-bench PRIVATE ../../../Domain/mplx-lang ../../../Application/mplx-compiler
                       ../../../Application/mplx-vm
)
target_link_libraries(mplx-bench PRIVATE mplx-lang mplx-compiler mplx-vm)
