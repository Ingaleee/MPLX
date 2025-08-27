add_executable(mplx
  main.cpp
)

find_package(fmt CONFIG REQUIRED)

target_include_directories(mplx PRIVATE ../../..\Domain\mplx-lang ../../..\Application\mplx-compiler ../../..\Application\mplx-vm)
target_link_libraries(mplx PRIVATE fmt::fmt domain application)