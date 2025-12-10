import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

const limit = 16 * 1024 * 1024;
function snapshot(value) {
    if (
        typeof value !== 'string' ||
        value.length > 2 * limit ||
        value.length % 2 ||
        !/^[0-9a-f]+$/.test(value)
    ) {
        throw new Error('Invalid canonical snapshot hex');
    }
    return Buffer.from(value, 'hex');
}
function fingerprint(bytes) {
    let value = 14695981039346656037n;
    for (const byte of bytes) {
        value = BigInt.asUintN(64, (value ^ BigInt(byte)) * 1099511628211n);
    }
    return `fnv1a64:${value.toString(16).padStart(16, '0')}`;
}
export function replayTrace(cli, trace) {
    if (
        trace.schema_version !== 1 ||
        trace.snapshot_version !== 1 ||
        trace.kind !== 'mplx.optimization-trace' ||
        !Array.isArray(trace.snapshots) ||
        trace.snapshots.length < 1 ||
        trace.snapshots.length > 64 ||
        !Array.isArray(trace.report?.passes) ||
        trace.report.passes.length !== trace.snapshots.length
    ) {
        throw new Error('Invalid optimization trace schema');
    }
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mplx-replay-'));
    const input = path.join(temporary, 'before.mplxir');
    const output = path.join(temporary, 'after.mplxir');
    const results = [];
    let previous;
    try {
        for (let index = 0; index < trace.snapshots.length; ++index) {
            const item = trace.snapshots[index];
            const record = trace.report.passes[index];
            if (
                typeof item.pass !== 'string' ||
                !/^[a-z0-9-]+$/.test(item.pass) ||
                item.pass !== record.name
            ) {
                throw new Error('Trace pass identity mismatch');
            }
            const before = snapshot(item.before_snapshot_hex);
            const expected = snapshot(item.after_snapshot_hex);
            if (
                (previous && !before.equals(previous)) ||
                fingerprint(before) !== record.before_fingerprint ||
                fingerprint(expected) !== record.after_fingerprint
            ) {
                throw new Error(`Trace content mismatch at ${item.pass}`);
            }
            fs.writeFileSync(input, before);
            const replay = spawnSync(cli, ['--replay-pass', '--pass', item.pass, '--out', output, input], {
                encoding: 'utf8',
                timeout: 30000,
                maxBuffer: 128 * 1024 * 1024,
                cwd: temporary,
            });
            if (replay.error) {
                throw replay.error;
            }
            if (replay.status !== 0) {
                throw new Error(`Pass ${item.pass} failed: ${replay.stderr}`);
            }
            if (!fs.readFileSync(output).equals(expected)) {
                throw new Error(`Pass ${item.pass} produced a different canonical IR`);
            }
            results.push({ pass: item.pass, verified: true, fingerprint: fingerprint(expected) });
            previous = expected;
        }
        return { schema_version: 1, replayed_passes: results };
    } finally {
        fs.rmSync(temporary, { recursive: true, force: true });
    }
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
    const [cli, file] = process.argv.slice(2);
    if (!cli || !file || fs.statSync(file).size > 256 * 1024 * 1024) {
        throw new Error('Usage: node scripts/replay-trace.mjs CLI TRACE.json (maximum 256 MiB)');
    }
    const result = replayTrace(path.resolve(cli), JSON.parse(fs.readFileSync(file, 'utf8')));
    process.stdout.write(`${JSON.stringify(result, null, 4)}\n`);
}
