add_executable(mplx main.cpp)

target_include_directories(
    mplx PRIVATE ../../../Domain/mplx-lang ../../../Application/mplx-compiler ../../../Application/mplx-vm
)
target_link_libraries(mplx PRIVATE mplx-lang mplx-compiler mplx-vm)
target_compile_definitions(
    mplx PRIVATE MPLX_BUILD_CONFIGURATION="$<CONFIG>" MPLX_VERSION="${PROJECT_VERSION}"
)

if(MINGW)
    target_link_options(mplx PRIVATE -static -static-libstdc++ -static-libgcc)
endif()

target_compile_definitions(mplx PRIVATE $<$<BOOL:${MPLX_WITH_JIT}>:MPLX_WITH_JIT=1>)
