add_library(mplx-jit jit_compiler.cpp jit_runtime.cpp machine_ir.cpp platform_win.cpp platform_posix.cpp)
target_include_directories(mplx-jit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-jit PUBLIC mplx-compiler)
