// Independent format-1.0 encoder. Does not call the compiler or serializer.
// Regenerate the committed golden file: node Presentation/fuzz/module_fixture.mjs
import { mkdirSync, writeFileSync } from 'node:fs';
const names = Buffer.from('main'),
    functions = Buffer.alloc(24),
    constants = Buffer.alloc(16);
functions.writeUInt32LE(4, 4); // name length; entry/local/arity/parameter offset = 0
functions[19] = 1; // i64 result
constants.writeBigInt64LE(42n);
constants.writeBigInt64LE(-9223372036854775808n, 8);
const code = Buffer.from([0, 0, 0, 0, 0, 32, 34]); // PUSH_CONST 0, RET, HALT
const sections = [names, functions, constants, code, Buffer.alloc(0), Buffer.alloc(0)];
const strides = [1, 24, 8, 1, 28, 1],
    bytes = Buffer.alloc(256 + sections.reduce((n, b) => n + b.length, 0));
bytes.write('MPLXBC\r\n');
bytes.writeUInt16LE(1, 8);
bytes.writeUInt32LE(64, 12);
bytes.writeBigUInt64LE(1n, 16);
bytes.writeBigUInt64LE(BigInt(bytes.length), 24);
bytes.writeUInt32LE(6, 32);
bytes.writeUInt32LE(32, 36);
bytes.writeBigUInt64LE(64n, 40);
let offset = 256;
sections.forEach((section, i) => {
    const at = 64 + i * 32;
    bytes.writeUInt32LE(i + 1, at);
    bytes.writeUInt32LE(strides[i], at + 4);
    bytes.writeBigUInt64LE(BigInt(offset), at + 8);
    bytes.writeBigUInt64LE(BigInt(section.length), at + 16);
    bytes.writeBigUInt64LE(BigInt(section.length / strides[i]), at + 24);
    section.copy(bytes, offset);
    offset += section.length;
});
// Bit-by-bit CRC deliberately differs from the production table implementation.
function crc(data) {
    let c = 0xffffffff;
    for (const b of data) {
        c ^= b;
        for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (c & 1 ? 0x82f63b78 : 0);
    }
    return ~c >>> 0;
}
bytes.writeUInt32LE(crc(bytes.subarray(64)), 48);
bytes.writeUInt32LE(crc(bytes.subarray(0, 64)), 52);
mkdirSync(new URL('corpus/module/', import.meta.url), { recursive: true });
writeFileSync(new URL('corpus/module/constant.mplxb', import.meta.url), bytes);
console.log(`Independent module fixture: ${bytes.length} bytes`);
