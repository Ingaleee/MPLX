import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
const cli = process.argv[2];
const child = spawn(cli, ['--analysis-server'], { stdio: 'pipe', windowsHide: true });
const pending = new Map();
let buffer = '';
child.stdout.setEncoding('utf8');
child.stdout.on('data', (data) => {
    buffer += data;
    for (;;) {
        const next = buffer.indexOf('\n');
        if (next < 0) break;
        const message = JSON.parse(buffer.slice(0, next));
        buffer = buffer.slice(next + 1);
        pending.get(message.id)?.(message.result);
        pending.delete(message.id);
    }
});
function analyze(id, source) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('compiler service timeout')), 10000);
        pending.set(id, (value) => {
            clearTimeout(timer);
            resolve(value);
        });
        const bytes = Buffer.from(source);
        child.stdin.write('ANALYZE ' + id + ' ' + bytes.length + '\n');
        child.stdin.write(bytes);
    });
}
try {
    const responses = await Promise.all(
        Array.from({ length: 30 }, (_, id) =>
            analyze(id, '// 😀 utf8\r\nfn main()->i64{let x=' + id + ';return x;}'),
        ),
    );
    for (let i = 0; i < responses.length; ++i) {
        const r = responses[i];
        assert.equal(r.schema_version, 1);
        assert.equal(r.diagnostics.length, 0);
        assert.ok(r.semantic.symbols.some((s) => s.name === 'x' && s.type === 'i64'));
        assert.equal(r.semantic.occurrences.length, 3);
    }
    const invalid = await analyze(50, 'fn main()->i64{return unknown;}');
    assert.equal(invalid.diagnostics[0].code, 'MPLX2006');
    assert.equal(invalid.diagnostics[0].range.end.offset - invalid.diagnostics[0].range.start.offset, 7);
    assert.equal((await analyze(51, 'fn main()->i64{return 4;}')).diagnostics.length, 0);
    const ir = spawnSync(cli, ['--dump-ir', '--no-opt', process.argv[3]], { encoding: 'utf8' });
    assert.equal(ir.status, 0, ir.stderr);
    assert.ok(JSON.parse(ir.stdout).functions[0].blocks.length);
    for (const input of ['BAD 1 0\n', 'ANALYZE 1 1048577\n', 'ANALYZE 1 2\nx']) {
        assert.equal(
            spawnSync(cli, ['--analysis-server'], { input, encoding: 'utf8', timeout: 5000 }).status,
            2,
        );
    }
    console.log(
        'Persistent compiler protocol: 30 pipelined frames, UTF8, recovery, malformed framing passed.',
    );
} finally {
    child.stdin.end();
    child.kill();
}
