add_library(mplx-jit
  x64_emitter.hpp
  platform.hpp
  platform_win.cpp
  platform_posix.cpp
  jit_runtime.hpp
  jit_runtime.cpp
  jit_compiler.hpp
  jit_compiler.cpp
)

target_include_directories(mplx-jit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler ../mplx-vm)


