add_executable(mplx-tests
  lexer_tests.cpp
)

target_include_directories(mplx-tests PRIVATE ../../../Domain/mplx-lang ../../../Application/mplx-compiler ../../../Application/mplx-vm)
target_link_libraries(mplx-tests PRIVATE mplx-lang mplx-compiler mplx-vm)

# Link ORM tests only if ORM is built
if (TARGET mplx-orm)
  target_include_directories(mplx-tests PRIVATE ../../../Infrastructure/mplx-orm)
  target_link_libraries(mplx-tests PRIVATE mplx-orm)
endif()