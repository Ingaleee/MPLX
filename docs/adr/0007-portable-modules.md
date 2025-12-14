# ADR 0007: canonical portable module container

Status: accepted for 0.5 / file format 1.0.

Persist verified bytecode rather than ASTs or generated native code. Native code
depends on host ABI, architecture and helper addresses; bytecode has fixed
instruction semantics and can be validated independently after loading.

The format uses fixed-width little-endian fields, six canonical sections, typed
exports, source mappings and CRC32C over header/directory/payload. Exact ordered
layouts make serialization deterministic and reduce overlapping-reference
ambiguity. Unknown features/versions fail explicitly. Counts, products and
ranges are bounded before payload allocations. Verification is mandatory even
when both checksums match.

A section directory allows future format versions to evolve deliberately.
Version 1.0 does not silently skip unknown sections or accept newer minor
versions. Authentication/signing and cross-file imports are separate contracts.
Source-level type proofs remain the typed IR verifier's responsibility.

CLI build writes through atomic replacement and refuses to overwrite its input.
C/.NET load/save the identical format. A standalone JavaScript encoder fixes an
independent golden artifact, including a negative signed constant. Deterministic
mutations cover corruption, repaired checksums, truncation, directory/signature/
source-map errors and invalid bytecode. Coverage-guided module fuzzing compares
accepted programs across bounded interpreter and native execution.

See MODULE_FORMAT.md for the exact wire contract, limits and compatibility policy.
