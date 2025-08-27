add_library(mplx-vm vm.cpp)

target_include_directories(mplx-vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler)
target_link_libraries(mplx-vm PUBLIC mplx-compiler)