add_executable(mplx-net main.cpp)
find_package(asio CONFIG REQUIRED)
target_link_libraries(mplx-net PRIVATE infrastructure asio::asio)