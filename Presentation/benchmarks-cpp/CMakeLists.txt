add_executable(mplx-bench
  bench_vm.cpp
)

target_include_directories(mplx-bench PRIVATE ../../../Domain/mplx-lang ../../../Application/mplx-compiler ../../../Application/mplx-vm)
target_link_libraries(mplx-bench PRIVATE mplx-lang mplx-compiler mplx-vm)

