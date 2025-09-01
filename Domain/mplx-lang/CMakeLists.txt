add_library(mplx-lang
  lexer.cpp
  parser.cpp
)

target_include_directories(mplx-lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})