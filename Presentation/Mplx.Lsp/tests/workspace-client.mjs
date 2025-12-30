import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync, symlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { CompilerClient } from '../dist/compiler-client.js';

const cli = resolve(process.argv[2]);
const directory = mkdtempSync(join(tmpdir(), 'mplx-client-workspace-'));
const client = new CompilerClient(cli);
const main = 'import "lib.mplx" as lib;fn main()->i64{return lib::value();}';
try {
    writeFileSync(join(directory, 'main.mplx'), main);
    writeFileSync(join(directory, 'lib.mplx'), 'export fn value()->i64{return 42;}');
    const first = await client.workspace(directory, 'main.mplx', []);
    assert.equal(first.schema_version, 2);
    assert.deepEqual(first.diagnostics, []);
    assert.equal(first.statistics.parsed, 2);
    const cached = await client.workspace(directory, 'main.mplx', []);
    assert.equal(cached.statistics.checked, 0);
    assert.equal(cached.statistics.syntax_hits, 2);
    const body = await client.workspace(directory, 'main.mplx', [
        { path: 'lib.mplx', source: '// new position\nexport fn value()->i64{return 43;}' },
    ]);
    assert.equal(body.statistics.parsed, 1);
    assert.equal(body.statistics.checked, 1);
    const imported = body.files
        .find((file) => file.path === 'main.mplx')
        .analysis.semantic.symbols.find((symbol) => symbol.imported);
    assert.equal(imported.declaration.start.line, 2);
    const changed = await client.workspace(directory, 'main.mplx', [
        { path: 'lib.mplx', source: 'export fn value(n:i64)->i64{return n;}' },
    ]);
    assert.equal(changed.statistics.checked, 2);
    assert.ok(changed.diagnostics.some((value) => value.path === 'main.mplx' && value.code === 'MPLX2018'));
    const closed = await client.workspace(directory, 'main.mplx', []);
    assert.deepEqual(closed.diagnostics, []);
    const escape = await client.workspace(directory, '../escape.mplx', []);
    assert.equal(escape.diagnostics[0].code, 'MPLX4001');
    const duplicate = await client.workspace(directory, 'main.mplx', [
        { path: 'main.mplx', source: main },
        { path: './main.mplx', source: main },
    ]);
    assert.equal(duplicate.diagnostics[0].code, 'MPLX4001');
    await assert.rejects(
        client.workspace(directory, 'main.mplx', [
            { path: 'main.mplx', source: 'x'.repeat(1024 * 1024 + 1) },
        ]),
        /limit/,
    );
    if (process.platform !== 'win32') {
        const outside = mkdtempSync(join(tmpdir(), 'mplx-outside-'));
        try {
            writeFileSync(join(outside, 'secret.mplx'), 'export fn secret()->i64{return 1;}');
            symlinkSync(join(outside, 'secret.mplx'), join(directory, 'escaped.mplx'));
            const symlink = await client.workspace(directory, 'main.mplx', [
                {
                    path: 'main.mplx',
                    source: 'import "escaped.mplx" as x;fn main()->i64{return x::secret();}',
                },
            ]);
            assert.ok(symlink.diagnostics.some((value) => value.code === 'MPLX4001'));
        } finally {
            rmSync(outside, { recursive: true, force: true });
        }
    }
    for (const input of [
        'X'.repeat(257) + '\n',
        'ANALYZE 1 0',
        'WORKSPACE 1 0 1 0\n',
        'WORKSPACE 1 1 1 257\n',
        'WORKSPACE 1 1 1 1\nrmFILE 1 1048577\n',
        'WORKSPACE 1 1 1 1\nrmBAD 1 1\n',
    ]) {
        const result = spawnSync(cli, ['--analysis-server'], { input, encoding: 'utf8', timeout: 5000 });
        assert.equal(result.status, 2);
    }
    assert.deepEqual((await client.analyze('fn main()->i64{return 42;}')).diagnostics, []);
    client.dispose();
    assert.deepEqual((await client.workspace(directory, 'main.mplx', [])).diagnostics, []);
    console.log(
        'Workspace service: cache invalidation, foreign spans, overlay closure, root/symlink bounds, malformed frames and restart passed.',
    );
} finally {
    client.dispose();
    rmSync(directory, { recursive: true, force: true });
}
