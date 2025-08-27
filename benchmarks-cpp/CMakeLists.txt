add_executable(mplx-bench
  bench_vm.cpp
)

find_package(benchmark CONFIG REQUIRED)
target_link_libraries(mplx-bench PRIVATE benchmark::benchmark mplx-lang mplx-compiler mplx-vm)

