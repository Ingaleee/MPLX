# Clean Architecture aggregate targets (non-invasive)

# Domain: core models and contracts (language, AST, bytecode definitions)
add_library(mplx-domain INTERFACE)
target_link_libraries(mplx-domain INTERFACE mplx-lang)

# Application: business rules (compiler, VM)
add_library(mplx-application INTERFACE)
target_link_libraries(mplx-application INTERFACE mplx-compiler mplx-vm)

# Infrastructure: external systems (ORM/DB, networking, packaging, C API)
add_library(mplx-infrastructure INTERFACE)
target_link_libraries(mplx-infrastructure INTERFACE mplx-orm mplx-net mplx-pkg mplx-capi)

# Presentation: entry points / user interfaces (executables)
# Note: executables are defined elsewhere; this interface exists for dependency grouping
add_library(mplx-presentation INTERFACE)
target_link_libraries(mplx-presentation INTERFACE mplx-domain mplx-application mplx-infrastructure)


