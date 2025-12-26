import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { CompilerClient } from '../dist/compiler-client.js';
const cli = resolve(process.argv[2]);
const client = new CompilerClient(cli);
try {
    const requests = Array.from({ length: 64 }, () => client.analyze('fn main()->i64{return 42;}'));
    await assert.rejects(client.analyze('fn main()->i64{return 7;}'), /queue is full/);
    for (const analysis of await Promise.all(requests)) assert.deepEqual(analysis.diagnostics, []);
    assert.deepEqual((await client.analyze('fn main()->i64{return 7;}')).diagnostics, []);
    const large = '//' + 'x'.repeat(1024 * 1024 - 64) + '\nfn main()->i64{return 42;}';
    const bytes = Array.from({ length: 16 }, () => client.analyze(large));
    await assert.rejects(client.analyze(large), /queue is full/);
    for (const analysis of await Promise.all(bytes)) assert.deepEqual(analysis.diagnostics, []);
    await assert.rejects(client.analyze('x'.repeat(1024 * 1024 + 1)), /1 MiB/);
    client.dispose();
    assert.deepEqual((await client.analyze('fn main()->i64{return 42;}')).diagnostics, []);
    console.log('Compiler client: bounded count/bytes, queue drain, source size and process restart passed.');
} finally {
    client.dispose();
}
