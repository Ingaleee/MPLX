add_executable(mplx-pkg main.cpp)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(mplx-pkg PRIVATE nlohmann_json::nlohmann_json)