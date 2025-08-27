add_executable(mplx-tests
  lexer_tests.cpp
  parser_smoke.cpp
  parser_errors.cpp
  orm_smoke.cpp
)

find_package(GTest CONFIG REQUIRED)
target_link_libraries(mplx-tests PRIVATE GTest::gtest_main mplx-lang mplx-orm)
include(GoogleTest)
gtest_discover_tests(mplx-tests)