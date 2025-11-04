add_library(mplx-reference reference.cpp)
target_include_directories(mplx-reference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-reference PUBLIC mplx-lang)
