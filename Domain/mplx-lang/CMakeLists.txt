add_library(mplx-lang
  lexer.cpp
  parser.cpp
)

find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(mplx-lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-lang PUBLIC nlohmann_json::nlohmann_json)