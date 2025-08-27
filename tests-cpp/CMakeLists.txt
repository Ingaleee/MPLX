add_executable(mplx-tests
  lexer_tests.cpp
)

find_package(GTest CONFIG REQUIRED)
target_link_libraries(mplx-tests PRIVATE GTest::gtest_main mplx-lang)
include(GoogleTest)
gtest_discover_tests(mplx-tests)