add_library(mplx-orm db.cpp)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_include_directories(mplx-orm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-orm PUBLIC unofficial::sqlite3::sqlite3)

