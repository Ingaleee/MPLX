# Clean Architecture layers mapped onto existing targets

# Domain: core contracts/models (language)
add_library(domain INTERFACE)
target_link_libraries(domain INTERFACE mplx-lang)

# Application: business rules (compiler + VM); depends on Domain only
add_library(application INTERFACE)
target_link_libraries(application INTERFACE mplx-compiler mplx-vm domain)

# Infrastructure: external systems (DB, net, pkg, native API); depends on Domain
add_library(infrastructure INTERFACE)
target_link_libraries(infrastructure INTERFACE mplx-orm mplx-net mplx-pkg mplx-capi domain)

# Presentation: entry points (CLI/tools/LSP/.NET sample); composes layers
add_library(presentation INTERFACE)
target_link_libraries(presentation INTERFACE domain application infrastructure)
