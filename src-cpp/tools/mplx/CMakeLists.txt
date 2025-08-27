add_executable(mplx
  main.cpp
)

find_package(fmt CONFIG REQUIRED)

target_include_directories(mplx PRIVATE ../mplx-lang ../mplx-compiler ../mplx-vm)
target_link_libraries(mplx PRIVATE fmt::fmt mplx-lang mplx-compiler mplx-vm)