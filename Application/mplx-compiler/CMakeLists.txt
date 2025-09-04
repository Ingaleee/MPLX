add_library(mplx-compiler
  compiler.cpp
)

target_include_directories(mplx-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../Domain/mplx-lang)
target_link_libraries(mplx-compiler PUBLIC mplx-lang)

target_compile_definitions(mplx-compiler PUBLIC
  $<$<BOOL:${MPLX_WITH_JIT}>:MPLX_WITH_JIT=1>
)