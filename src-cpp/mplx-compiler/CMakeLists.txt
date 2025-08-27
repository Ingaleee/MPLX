add_library(mplx-compiler
  compiler.cpp
)

target_include_directories(mplx-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-lang)
target_link_libraries(mplx-compiler PUBLIC mplx-lang)