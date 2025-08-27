add_library(mplx-net net.cpp)
find_package(asio CONFIG REQUIRED)
target_include_directories(mplx-net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-net PUBLIC asio::asio)