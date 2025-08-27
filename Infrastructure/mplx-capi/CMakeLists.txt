add_library(mplx-capi SHARED capi.cpp)
find_package(nlohmann_json CONFIG REQUIRED)

# РРјСЏ РІС‹С…РѕРґРЅРѕР№ DLL
set_target_properties(mplx-capi PROPERTIES OUTPUT_NAME "mplx_native")

target_include_directories(mplx-capi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../mplx-compiler ../mplx-lang ../mplx-vm)
target_link_libraries(mplx-capi PUBLIC mplx-lang mplx-compiler mplx-vm nlohmann_json::nlohmann_json)