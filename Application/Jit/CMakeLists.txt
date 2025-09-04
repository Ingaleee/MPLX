add_library(mplx-jit
  x64_emitter.hpp
  jit_runtime.hpp
  jit_runtime.cpp
)

target_include_directories(mplx-jit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler ../mplx-vm)


