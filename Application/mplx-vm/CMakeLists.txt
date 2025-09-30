add_library(mplx-vm vm.cpp)

target_include_directories(mplx-vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler)
target_link_libraries(mplx-vm PUBLIC mplx-compiler)

target_compile_definitions(mplx-vm PUBLIC
  $<$<BOOL:${MPLX_WITH_JIT}>:MPLX_WITH_JIT=1>
)

if (MPLX_WITH_JIT)
  target_link_libraries(mplx-vm PUBLIC mplx-jit)
endif()