# A five-minute MPLX review

1. Build/test with the README commands. The C++20 core has no third-party
   dependencies. Install the LSP dependencies before CMake configuration to
   include the process-level LSP suite in CTest.
2. Run examples/runtime_showcase.mplx: shadowing, short-circuiting, calls,
   scopes and a loop; result 5050.
3. Inspect --dump-ir, --dump-bytecode and --dump-cfg. Follow the typed virtual
   values, basic blocks, source spans and independently verified stack joins.
4. Run examples/limits.mplx with --max-steps 100. Expect a bounded error with
   function, PC and source location.
5. Run examples/native_calls.mplx with --jit on --jit-verify --stats --no-runfile.
   Require result 17711, jit_executions=1, jit_fallbacks=0 and native_calls=57326.
   The native-calls suite also verifies 6,000 language frames without host recursion.
6. Build the VS Code extension or run the LSP protocol test. Rename the shadowed
   n in runtime_showcase: the outer binding and comments must stay correct.
   Review UTF-16 positions, capture rejection and rapid-version diagnostic tests.
7. Compare the loop/call reports in docs/benchmarks. Check native_runs and
   native_calls before comparing p50/p95. Read docs/VALIDATION.md for executed
   platforms, sanitizer/fuzz outcomes and remaining limits.
8. Build examples/embedding.mplx with --build; inspect and execute its portable
   module with --entry transform --arg 21 --arg 2 --arg 1. Review the bounded
   loader, independent binary fixture and repaired-checksum module fuzzer.
9. Inspect VerifiedModule -> CompiledProgram -> VM ownership. The shared-program
   test starts eight workers together and requires one native compilation.
   The C11/.NET tests retain instances after releasing their program handles,
   exercise concurrent cancellation and recover after faults.
10. Execute examples/modules/main.mplx. Follow the import graph, direct export
    interfaces, prototype checking and real IR call relocation. The incremental
    suite distinguishes body/signature/declaration-position changes and checks
    unchanged artifact identity. The LSP workspace suites verify closed files,
    overlays, versioned multi-file rename and watched create/change/delete.

11. Run `--dump-analyses` and `--dump-pass-trace` on examples/ssa.mplx. Inspect
    phi edge liveness, loop nesting, trap/divergence summaries and range proofs.
    Replay the trace through scripts/replay-trace.mjs and compare canonical
    before/after artifacts. The conformance suite executes every intermediate
    pass result against authored scalar outcomes and the independent AST oracle.

Useful review questions: where verification is amortized, what a native helper can assume,
how instruction/frame/value budgets survive calls, which optimizations preserve
traps, how liveness determines slot interference, how rename preserves bindings,
and who owns each executable mapping/document snapshot. Distinguish corruption
checks from bytecode safety and source-level typing proofs.

The ADRs explain these decisions. Historical network/ORM/package projects and
old tracked build outputs are outside the maintained core; the roadmap identifies
the next coherent milestones.
