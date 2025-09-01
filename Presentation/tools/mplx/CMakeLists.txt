add_executable(mplx
  main.cpp
)

target_include_directories(mplx PRIVATE ../../../Domain/mplx-lang ../../../Application/mplx-compiler ../../../Application/mplx-vm)
target_link_libraries(mplx PRIVATE mplx-lang mplx-compiler mplx-vm)