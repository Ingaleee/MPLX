add_executable(mplx-net-tool main.cpp)
target_include_directories(mplx-net-tool PRIVATE ../../../Infrastructure/mplx-net)
target_link_libraries(mplx-net-tool PRIVATE mplx-net)