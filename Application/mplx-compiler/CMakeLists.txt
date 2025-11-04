add_library(
    mplx-compiler
    compiler.cpp
    bytecode.cpp
    verifier.cpp
    frontend.cpp
    semantic.cpp
    ir.cpp
    ir_analysis.cpp
    analysis_manager.cpp
    range_analysis.cpp
    ir_snapshot.cpp
    ir_passes.cpp
    ssa.cpp
    verified_module.cpp
    module_format.cpp
    workspace.cpp
)

target_include_directories(mplx-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../Domain/mplx-lang)
target_link_libraries(mplx-compiler PUBLIC mplx-lang)

target_compile_definitions(mplx-compiler PUBLIC $<$<BOOL:${MPLX_WITH_JIT}>:MPLX_WITH_JIT=1>)
