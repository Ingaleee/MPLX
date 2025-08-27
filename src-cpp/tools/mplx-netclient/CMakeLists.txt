add_executable(mplx-netclient main.cpp)
find_package(asio CONFIG REQUIRED)
target_link_libraries(mplx-netclient PRIVATE asio::asio)

