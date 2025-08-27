# MPLX Clean Architecture Mapping

- Domain: language primitives and contracts
  - Targets: mplx-domain (interfaces) links to mplx-lang
  - Ownership: AST, tokens, parsing contracts
- Application: business rules (compile & execute)
  - Targets: mplx-application links to mplx-compiler, mplx-vm
  - Ownership: compilation strategies, optimizations, VM semantics
- Infrastructure: external systems
  - Targets: mplx-infrastructure links to mplx-orm, mplx-net, mplx-pkg, mplx-capi
  - Ownership: SQLite/DB, networking (ASIO), packaging, native interop
- Presentation: user interfaces
  - Targets: mplx-presentation aggregates Domain+Application+Infrastructure
  - Entry points: CLI (mplx), tools (mplx-net, mplx-netclient), LSP server, VS Code extension, .NET sample

Dependency rule: Presentation -> Application -> Domain; Infrastructure depends on Domain but not vice versa. Application must not depend on Infrastructure.

This layer mapping is non-invasive: existing concrete targets remain unchanged; aggregate interface targets provide architectural boundaries and can be used by tooling and CI checks.
