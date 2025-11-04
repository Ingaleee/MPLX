add_executable(mplx-tests runtime_tests.cpp)
add_executable(mplx-analysis-tests analysis_tests.cpp)
target_link_libraries(mplx-analysis-tests PRIVATE mplx-vm mplx-reference)
add_test(NAME mplx.analyses COMMAND mplx-analysis-tests)
set_tests_properties(mplx.analyses PROPERTIES TIMEOUT 60)
add_executable(mplx-conformance-tests conformance_tests.cpp)
target_link_libraries(mplx-conformance-tests PRIVATE mplx-vm mplx-reference)
add_test(NAME mplx.conformance COMMAND mplx-conformance-tests)
set_tests_properties(mplx.conformance PROPERTIES TIMEOUT 60)
if(MPLX_WITH_JIT)
    add_executable(mplx-machine-tests machine_tests.cpp)
    target_link_libraries(mplx-machine-tests PRIVATE mplx-vm)
    add_test(NAME mplx.machine COMMAND mplx-machine-tests)
    set_tests_properties(mplx.machine PROPERTIES TIMEOUT 60)
endif()
add_executable(mplx-reference-tests reference_tests.cpp)
target_link_libraries(mplx-reference-tests PRIVATE mplx-vm mplx-reference)
add_test(NAME mplx.reference COMMAND mplx-reference-tests)
set_tests_properties(mplx.reference PROPERTIES TIMEOUT 60)
add_executable(mplx-ssa-tests ssa_tests.cpp)
target_link_libraries(mplx-ssa-tests PRIVATE mplx-vm)
add_test(NAME mplx.ssa COMMAND mplx-ssa-tests)
set_tests_properties(mplx.ssa PROPERTIES TIMEOUT 60)
add_executable(mplx-workspace-tests workspace_tests.cpp)
target_link_libraries(mplx-workspace-tests PRIVATE mplx-vm)
foreach(suite modules incremental dependency-graph)
    add_test(NAME mplx.${suite} COMMAND mplx-workspace-tests ${suite})
    set_tests_properties(mplx.${suite} PROPERTIES TIMEOUT 60)
endforeach()
add_executable(mplx-advanced-tests advanced_tests.cpp)
find_package(Threads REQUIRED)
add_executable(mplx-module-tests module_tests.cpp)
target_link_libraries(mplx-module-tests PRIVATE mplx-vm mplx-capi Threads::Threads)
add_test(NAME mplx.module-format COMMAND mplx-module-tests module-format
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../fuzz/corpus/module/constant.mplxb
)
set_tests_properties(mplx.module-format PROPERTIES TIMEOUT 60)
foreach(suite shared-program embedding-contexts)
    add_test(NAME mplx.${suite} COMMAND mplx-module-tests ${suite})
    set_tests_properties(mplx.${suite} PROPERTIES TIMEOUT 60)
endforeach()
add_executable(mplx-c-embedding c_embedding.c)
target_link_libraries(mplx-c-embedding PRIVATE mplx-capi)
set_target_properties(mplx-c-embedding PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
add_test(NAME mplx.c-embedding COMMAND mplx-c-embedding)
target_link_libraries(mplx-advanced-tests PRIVATE mplx-vm)
foreach(suite typed-ir native-calls)
    add_test(NAME mplx.${suite} COMMAND mplx-advanced-tests ${suite})
    set_tests_properties(mplx.${suite} PROPERTIES TIMEOUT 60)
endforeach()
target_link_libraries(mplx-tests PRIVATE mplx-lang mplx-compiler mplx-vm mplx-capi)
foreach(suite frontend compiler verifier runtime differential capi)
    add_test(NAME mplx.${suite} COMMAND mplx-tests ${suite})
    set_tests_properties(mplx.${suite} PROPERTIES TIMEOUT 60)
endforeach()
find_program(MPLX_NODE_EXECUTABLE node)
if(MPLX_NODE_EXECUTABLE)
    add_test(NAME mplx.conformance-manifest COMMAND ${MPLX_NODE_EXECUTABLE}
                                                    ${PROJECT_SOURCE_DIR}/scripts/conformance.mjs --check
    )
endif()
foreach(name source bytecode module workspace ir snapshot)
    add_executable(mplx-seeds-${name} seed_runner.cpp ../fuzz/fuzz_${name}.cpp)
    target_link_libraries(mplx-seeds-${name} PRIVATE mplx-vm mplx-reference)
    add_test(NAME mplx.fuzz-seeds-${name} COMMAND mplx-seeds-${name}
                                                  ${CMAKE_CURRENT_SOURCE_DIR}/../fuzz/corpus/${name}
    )
    set_tests_properties(mplx.fuzz-seeds-${name} PROPERTIES TIMEOUT 60)
endforeach()
if(MPLX_NODE_EXECUTABLE)
    if(EXISTS "${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/node_modules/vscode-languageserver/package.json"
       AND EXISTS "${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/dist/compiler-client.js"
    )
        add_test(NAME mplx.lsp
                 COMMAND ${MPLX_NODE_EXECUTABLE}
                         ${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/tests/protocol.mjs $<TARGET_FILE:mplx>
        )
        set_tests_properties(mplx.lsp PROPERTIES TIMEOUT 60)
        add_test(
            NAME mplx.lsp-ipc
            COMMAND ${MPLX_NODE_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/tests/protocol.mjs
                    $<TARGET_FILE:mplx> --ipc
        )
        set_tests_properties(mplx.lsp-ipc PROPERTIES TIMEOUT 60)
        add_test(
            NAME mplx.compiler-client
            COMMAND ${MPLX_NODE_EXECUTABLE}
                    ${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/tests/compiler-client.mjs $<TARGET_FILE:mplx>
        )
        set_tests_properties(mplx.compiler-client PROPERTIES TIMEOUT 60)
        foreach(transport stdio ipc)
            add_test(
                NAME mplx.lsp-workspace-${transport}
                COMMAND
                    ${MPLX_NODE_EXECUTABLE}
                    ${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/tests/workspace-protocol.mjs
                    $<TARGET_FILE:mplx> --${transport}
            )
            set_tests_properties(mplx.lsp-workspace-${transport} PROPERTIES TIMEOUT 60)
        endforeach()
        add_test(
            NAME mplx.workspace-service
            COMMAND
                ${MPLX_NODE_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/Presentation/Mplx.Lsp/tests/workspace-client.mjs $<TARGET_FILE:mplx>
        )
        set_tests_properties(mplx.workspace-service PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME mplx.compiler-service
             COMMAND ${MPLX_NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compiler_service_tests.mjs
                     $<TARGET_FILE:mplx> ${PROJECT_SOURCE_DIR}/examples/native_loop.mplx
    )
    set_tests_properties(mplx.compiler-service PROPERTIES TIMEOUT 60)
    add_test(NAME mplx.cli COMMAND ${MPLX_NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.mjs
                                   $<TARGET_FILE:mplx> $<BOOL:${MPLX_WITH_JIT}>
    )
    set_tests_properties(mplx.cli PROPERTIES TIMEOUT 60)
endif()
