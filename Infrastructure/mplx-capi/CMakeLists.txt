add_library(mplx-capi SHARED capi.cpp)

# Имя выходной DLL
set_target_properties(mplx-capi PROPERTIES OUTPUT_NAME "mplx_native")

target_include_directories(mplx-capi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../Application/mplx-compiler ../../Domain/mplx-lang ../../Application/mplx-vm)
target_link_libraries(mplx-capi PUBLIC mplx-lang mplx-compiler mplx-vm)