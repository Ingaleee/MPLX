cmake_minimum_required(VERSION 3.26)
project(
    mplx
    VERSION 0.6.0
    LANGUAGES C CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<0:>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<0:>")

# Build toggles
option(MPLX_BUILD_TESTS "Build C++ conformance and regression tests" ON)
option(MPLX_BUILD_ORM "Build SQLite ORM module" OFF)
option(MPLX_BUILD_BENCH "Build benchmarks (Presentation/benchmarks-cpp)" OFF)
option(MPLX_BUILD_NET "Build network library and tools" OFF)
option(MPLX_BUILD_PKG "Build package tool (mplx-pkg)" OFF)
option(MPLX_WITH_JIT "Enable experimental JIT compiler" OFF)
option(MPLX_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(MPLX_THREAD_SANITIZER "Enable thread sanitizer (separate from ASan)" OFF)
option(MPLX_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
if(MPLX_THREAD_SANITIZER)
    if(MSVC
       OR MPLX_SANITIZERS
       OR MPLX_BUILD_FUZZERS
    )
        message(FATAL_ERROR "Thread sanitizer requires GCC/Clang and a separate build")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
if(MPLX_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "MPLX_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(MPLX_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Fuzz targets require Clang")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=fuzzer-no-link,address,undefined)
endif()
if(MPLX_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(Domain/mplx-lang)
add_subdirectory(Application/mplx-reference)
add_subdirectory(Application/mplx-compiler)
add_subdirectory(Application/mplx-vm)
if(MPLX_WITH_JIT)
    add_subdirectory(Application/Jit)
endif()
add_subdirectory(Presentation/tools/mplx)

add_subdirectory(Infrastructure/mplx-capi)

# Optional modules (guarded by toggles)
if(MPLX_BUILD_ORM)
    add_subdirectory(Infrastructure/mplx-orm)
endif()

if(MPLX_BUILD_BENCH)
    add_subdirectory(Presentation/benchmarks-cpp)
endif()

if(MPLX_BUILD_NET)
    add_subdirectory(Infrastructure/mplx-net)
    add_subdirectory(Presentation/tools/mplx-net)
    add_subdirectory(Presentation/tools/mplx-netclient)
endif()

if(MPLX_BUILD_PKG)
    add_subdirectory(Infrastructure/mplx-pkg)
endif()

if(MPLX_BUILD_TESTS)
    add_subdirectory(Presentation/tests-cpp)
endif()
if(MPLX_BUILD_FUZZERS)
    add_subdirectory(Presentation/fuzz)
endif()
include(cmake/Architecture.cmake)
include(GNUInstallDirs)
install(
    TARGETS mplx mplx-capi
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    FILES Infrastructure/mplx-capi/capi.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mplx
    RENAME mplx.h
)
install(
    DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mplx/examples
    FILES_MATCHING
    PATTERN "*.mplx"
)
install(FILES README.md docs/LANGUAGE.md docs/MODULE_FORMAT.md docs/EMBEDDING.md docs/MODULES.md
              docs/COMPILER_PIPELINE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/mplx/docs
)
install(
    DIRECTORY docs/language docs/compiler docs/vision
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mplx/docs
    FILES_MATCHING
    PATTERN "*.md"
    PATTERN "*.json"
)
install(FILES conformance/semantics.json DESTINATION ${CMAKE_INSTALL_DATADIR}/mplx/conformance)
install(FILES scripts/replay-trace.mjs DESTINATION ${CMAKE_INSTALL_DATADIR}/mplx/tools)
