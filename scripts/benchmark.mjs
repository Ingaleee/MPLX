import { spawnSync } from 'node:child_process';
import { cpus, platform, arch, release } from 'node:os';
import { resolve, dirname } from 'node:path';
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
const [binary, source, output, runs = '50'] = process.argv.slice(2);
if (!binary || !source || !output)
    throw new Error('Usage: node scripts/benchmark.mjs <mplx> <source.mplx> <report.json> [runs]');
const executable = resolve(binary),
    file = resolve(source);
function invoke(args) {
    const r = spawnSync(executable, args, { encoding: 'utf8', timeout: 120000 });
    if (r.error || r.status !== 0) throw new Error(r.error?.message ?? r.stderr ?? 'benchmark failed');
    return JSON.parse(r.stdout);
}
const off = invoke(['--bench', file, '--mode', 'run-only', '--jit', 'off', '--runs', runs, '--warmup', '5']);
const on = invoke(['--bench', file, '--mode', 'run-only', '--jit', 'on', '--runs', runs, '--warmup', '5']);
if (off.result !== on.result) throw new Error('result mismatch');
const report = {
    schema_version: 1,
    recorded_at: new Date().toISOString(),
    environment: {
        platform: platform(),
        arch: arch(),
        os_release: release(),
        cpu: cpus()[0]?.model,
        logical_cpus: cpus().length,
    },
    command: { binary, source, runs: Number(runs), warmup: 5 },
    source_sha256: createHash('sha256').update(readFileSync(file)).digest('hex'),
    interpreter: { ...off, file: source },
    native: { ...on, file: source },
    native_comparison_valid: on.native_runs === on.runs,
    median_ratio: on.native_runs === on.runs ? off.p50_ms / on.p50_ms : null,
};
mkdirSync(dirname(resolve(output)), { recursive: true });
writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
console.log('Saved ' + resolve(output) + '; native runs ' + on.native_runs + '/' + on.runs);
