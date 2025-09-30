cmake_minimum_required(VERSION 3.26)
project(mplx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build toggles
option(MPLX_BUILD_TESTS "Build C++ tests (Presentation/tests-cpp)" OFF)
option(MPLX_BUILD_ORM   "Build SQLite ORM module" OFF)
option(MPLX_BUILD_BENCH "Build benchmarks (Presentation/benchmarks-cpp)" OFF)
option(MPLX_BUILD_NET   "Build network library and tools" OFF)
option(MPLX_BUILD_PKG   "Build package tool (mplx-pkg)" OFF)
option(MPLX_WITH_JIT    "Enable experimental JIT compiler" OFF)

add_subdirectory(Domain/mplx-lang)
add_subdirectory(Application/mplx-compiler)
add_subdirectory(Application/mplx-vm)
if (MPLX_WITH_JIT)
  add_subdirectory(Application/Jit)
endif()
add_subdirectory(Presentation/tools/mplx)

add_subdirectory(Infrastructure/mplx-capi)

# Optional modules (guarded by toggles)
if (MPLX_BUILD_TESTS)
  add_subdirectory(Presentation/tests-cpp)
endif()

if (MPLX_BUILD_ORM)
  add_subdirectory(Infrastructure/mplx-orm)
endif()

if (MPLX_BUILD_BENCH)
  add_subdirectory(Presentation/benchmarks-cpp)
endif()

if (MPLX_BUILD_NET)
  add_subdirectory(Infrastructure/mplx-net)
  add_subdirectory(Presentation/tools/mplx-net)
  add_subdirectory(Presentation/tools/mplx-netclient)
endif()

if (MPLX_BUILD_PKG)
  add_subdirectory(Infrastructure/mplx-pkg)
endif()

