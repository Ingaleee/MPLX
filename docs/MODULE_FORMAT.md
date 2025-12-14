# MPLX portable module format 1.0

The .mplxb container stores verified bytecode, typed export signatures, signed
constants and source mappings. It never stores native addresses, executable
mappings, compiler ASTs or runtime counters. A loaded program can compile its
native image for the current platform.

All integer fields use little-endian fixed widths, independent of C++ struct
padding and host pointer size. Signed constants use 64-bit two's complement.
The reader supports exactly format 1.0 and feature mask 1; unknown versions,
flags, section kinds or reserved fields fail rather than being guessed.

## Header and directory

The header occupies 64 bytes; six 32-byte directory records immediately follow.
Payload begins at byte 256. CRC32C uses reflected polynomial 0x82f63b78,
initial/final XOR 0xffffffff. Test vectors are from
[RFC 3720 appendix B.4](https://www.rfc-editor.org/rfc/rfc3720#appendix-B.4).

| Header offset | Width | Meaning                                        |
| ------------- | ----- | ---------------------------------------------- |
| 0             | 8     | Magic: ASCII MPLXBC followed by CR LF          |
| 8 / 10        | 2 / 2 | Major 1 / minor 0                              |
| 12            | 4     | Header size: 64                                |
| 16            | 8     | Required features: 1 (typed exports)           |
| 24            | 8     | Exact complete file size                       |
| 32 / 36       | 4 / 4 | Section count 6 / directory stride 32          |
| 40            | 8     | Directory offset: 64                           |
| 48            | 4     | CRC32C of bytes [64, end), including directory |
| 52            | 4     | CRC32C of header, with this field zeroed       |
| 56            | 8     | Reserved: zero                                 |

A directory record contains kind:u32, stride:u32, offset:u64, byte_size:u64,
element_count:u64. Records must be in the order below. Sections are contiguous;
offsets, sizes and counts must agree exactly, including empty sections. Gaps,
overlaps, duplicate/unknown sections and trailing bytes are rejected.

| Kind | Section              | Stride | Count limit                               |
| ---- | -------------------- | ------ | ----------------------------------------- |
| 1    | Export name bytes    | 1      | 8 MiB                                     |
| 2    | Function records     | 24     | 65,536                                    |
| 3    | Signed constants     | 8      | 1,000,000                                 |
| 4    | Instruction bytes    | 1      | 16 MiB                                    |
| 5    | Source mappings      | 28     | 16 Mi entries, also bounded by total size |
| 6    | Parameter type bytes | 1      | 65,536 × 255                              |

The total file limit is 64 MiB. Bytecode verification additionally limits decoded
instructions to 2,000,000. Allocation sizes are checked against actual input
ranges, section counts, strides and the total limit before payload allocation.

## Records

Each function record contains name_offset:u32, name_length:u32, entry_pc:u32,
parameter_offset:u32, local_count:u16, arity:u8, return_type:u8, reserved:u32=0.
Names are nonempty canonical UTF-8, without NUL, at most 1,024 bytes each.
Name and parameter references consume their pools in function-record order,
without aliases, gaps or unreferenced bytes. Export names must be unique.
Type tags: 0=i32, 1=i64, 2=bool. Each parameter occupies one tag byte.

A source mapping contains instruction_pc:u32, then start and end positions,
each offset:u32, line:u32, column:u32. Source offsets/columns use UTF-8 bytes;
lines/columns are one-based. Empty mappings are allowed for host-built bytecode.
Mappings must be strictly ordered, reference instruction starts and have
nondecreasing start/end byte offsets.

Instruction numbers/immediate widths are defined in bytecode.hpp/bytecode.cpp.
Format 1.0 freezes this opcode schema; incompatible instruction semantics or
value representations require a new major format version.

## Validation and compatibility

Loading checks size, magic/version/features, header shape and both CRCs; then
directory bounds and record metadata; then independent instruction/CFG/stack
verification. Execution cannot begin until VerifiedModule construction succeeds.
CRC detects corruption; it is not an authenticity mechanism. A bytecode file can
be intentionally constructed with valid checksums.

The bytecode verifier establishes instruction and memory-access invariants,
operand heights and function boundaries. Full source-level type and definite-
assignment proofs belong to the typed IR verifier. Export arguments and return
values are checked at the host boundary, including bool's 0/1 representation.
Module loading does not claim a complete source-type proof for arbitrary
hand-authored bytecode.

Serialization is deterministic. Tests require byte-for-byte canonical round
trips, all truncation points, mutations with repaired CRCs and agreement with a
separate JavaScript encoder. The module fuzzer repairs CRCs to reach metadata and
bytecode validation, then compares bounded interpreter/native outcomes and
resource statistics for accepted inputs.

```sh
mplx --build examples/embedding.mplx --out build/embedding.mplxb
mplx --inspect-module build/embedding.mplxb
mplx --run-module build/embedding.mplxb --entry transform --arg 21 --arg 2 --arg 1 --jit on --jit-verify --no-runfile
```

The final command returns 42. Failed source compilation leaves an existing output
module intact. CLI writes use a temporary file and atomic replacement.
