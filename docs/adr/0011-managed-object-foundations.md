# ADR 0011: proposed managed object foundation

Status: proposed, pending an implemented and verified vertical type path.
This is a design contract for M07/M08/M24, not a description of current runtime behavior.

## Problem

The existing `ValueType` and runtime carrier support only `i32`, `i64` and `bool`.
Passing an object handle through that carrier as an ordinary integer would allow
forged references, confuse IR/bytecode verification and obscure GC roots.
Extending the syntax first would also leave the AST oracle, wire format, JIT and
embedding lifetime rules disagreeing about the same apparent type.

## Proposed representation

Introduce a versioned type table with explicit `TypeId` values and immutable
descriptors. Scalars describe range/carrier semantics. Reference types describe
their object kind, payload layout, element/field types, alignment and traced slots.
Type IDs are module-local; linking remaps them with exact descriptor compatibility.
Persisted artifacts carry descriptor and semantic feature versions. A stable
fingerprint labels descriptors but must not replace full equality checks.

Keep scalar and reference kinds distinct through resolution, typed IR, bytecode
verification and host APIs. A reference is an opaque heap-owned handle, containing
an allocation slot and a generation. Generation reuse must never wrap back to a
previously valid handle; retire a slot before exhaustion. Validate heap ownership,
generation and expected type on dereference. Nullability is explicit in types.
Public handles are opaque ownership objects, not exposed machine addresses.

Use an isolate-owned, bounded handle table and heap as the first implementation.
One execution instance owns its heap. Cross-instance references are rejected.
This avoids introducing shared-heap synchronization before M30 defines it.
Allocation checks object-count, logical-byte and address-overflow limits before
publication. Distinguish deterministic budget exhaustion from host allocator failure.
An unsuccessful allocation must not install a partially initialized object.

## Baseline object types

Start with immutable UTF-8 strings, checked arrays and fixed-layout records:

| Kind   | Required rules                                                                                              |
| ------ | ----------------------------------------------------------------------------------------------------------- |
| String | Valid Unicode scalar sequences, explicit byte length and scalar-index operations; reject invalid UTF-8      |
| Array  | Invariant element type, checked length/index arithmetic, defined mutation/aliasing and initialized elements |
| Record | Verified field types/order/layout, full initialization before publication and explicit field access         |

References alias objects. Assignment of a reference does not implicitly clone its
payload. A distinct cloning operation can be introduced later. Array storage may
move, so views must hold an owning handle with checked offset/length rather than a
raw payload pointer. Growth must define view invalidation or preserve backing
storage explicitly. Record value semantics are a separate language decision;
they must not emerge accidentally from the scalar carrier implementation.

The first vertical path should introduce one object kind end to end. No language
feature is enabled until AST, typed lowering, VM and native execution agree on
values, errors, visible mutations and source spans. Other descriptor kinds can
remain unsupported and rejected until their paths exist.

## Roots and collection

Implement a nonmoving tracing collector as the correctness baseline. Trace typed
frame locals, operand values, explicit native roots and retained host handles.
Traverse only descriptor-declared reference fields. Stable handles decouple logical
object identity from payload placement, allowing a later moving collector without
changing the reference ABI.

Collection is permitted only at explicit allocation/call/backedge safepoints for
which roots are complete. Native cached references require a root map or a spill
to typed root-visible storage before a safepoint. Until that contract exists, the
JIT must reject unsupported object programs or use a documented interpreter path;
it cannot treat an untracked register as an implicit root.

Host-held roots use scope-bound handles. Pinning is a separate operation for
bounded native access, with unpin cleanup at the exception boundary. A host must
not retain a borrowed payload pointer after its borrow/pin scope. FFI callbacks,
reentrant collection and thread attachment remain M27 work.

The collector's root set and descriptor edges should be compared against an
independent reachability model in state-machine tests. Stress mode collects before
every eligible allocation. Tests must cover cycles, stale generations, mismatched
types, nested calls, rooted/unrooted host references, allocation failure and reuse
after cancellation or a checked trap.

## Compatibility and execution policy

MPLXB 1.0 and C ABI v1 remain scalar contracts. New type/constant tables and object
marshalling need declared format/ABI evolution; they cannot silently reinterpret
existing `i64` values as handles. Existing scalar artifacts must remain loadable
where explicitly supported, with missing feature flags rejected before execution.

GC work, allocation count and logical heap bytes are separate execution budgets.
The semantic contract must define whether collection timing is observable through
host effects; the baseline should expose telemetry without language-level timing
dependence. Moving/generational/concurrent collectors are subsequent milestones,
not acceptance criteria for the first nonmoving baseline.

## Acceptance before implementation promotion

1. A documented typed reference representation and verified descriptor table.
2. A bounded heap with generation-safe handles and failure-atomic publication.
3. Independent reachability tests for explicit roots and tracing.
4. One complete object type through syntax, resolution, IR, serialization and execution.
5. AST/raw VM/optimized VM/native conformance, including mutation and faults.
6. Windows/Linux sanitizer and fuzz evidence, plus compile/execution/memory measurements.
7. Compatibility tests for old scalar artifacts and rejected unsupported features.

The codebase currently satisfies none of these object-runtime acceptance gates as
an integrated subsystem. The completed scalar analysis/replay package supplies
the verification and debugging infrastructure needed to build and review this work.
