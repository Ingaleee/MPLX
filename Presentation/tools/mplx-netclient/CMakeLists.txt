add_executable(mplx-netclient-tool main.cpp)
target_include_directories(mplx-netclient-tool PRIVATE ../../../Infrastructure/mplx-net)
target_link_libraries(mplx-netclient-tool PRIVATE mplx-net)

