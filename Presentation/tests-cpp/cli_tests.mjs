import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { replayTrace } from '../../scripts/replay-trace.mjs';
const cli = process.argv[2];
const jitEnabled = process.argv[3] === '1';
const dir = mkdtempSync(join(tmpdir(), 'mplx-cli-'));
const source = join(dir, 'program with spaces.mplx');
function invoke(args, status = 0) {
    const r = spawnSync(cli, args, { cwd: dir, encoding: 'utf8', timeout: 15000 });
    assert.ifError(r.error);
    assert.equal(r.status, status, r.stderr + r.stdout);
    return r;
}
try {
    writeFileSync(source, 'fn main()->i64{let x=40;return x+2;}');
    assert.equal(invoke(['--run', '--jit', 'off', '--no-runfile', source]).stdout.trim(), 'Result: 42');
    assert.equal(existsSync(join(dir, 'run.txt')), false);
    invoke(['--run', source]);
    assert.equal(readFileSync(join(dir, 'run.txt'), 'utf8').trim(), '42');
    const out = join(dir, 'nested', 'result.txt');
    invoke(['--run', '--out', out, source]);
    assert.equal(readFileSync(out, 'utf8').trim(), '42');
    assert.deepEqual(JSON.parse(invoke(['--check', source]).stdout), { diagnostics: [] });
    const b = JSON.parse(
        invoke([
            '--bench',
            source,
            '--mode',
            'run-only',
            '--runs',
            '7',
            '--warmup',
            '2',
            '--jit',
            'off',
            '--json',
        ]).stdout,
    );
    assert.equal(b.mode, 'run-only');
    assert.equal(b.runs, 7);
    assert.equal(b.warmup, 2);
    assert.equal(b.result, 42);
    assert.equal(b.samples_ms.length, 7);
    assert.equal(b.native_runs, 0);
    assert.ok(b.best_ms <= b.p50_ms && b.p50_ms <= b.p95_ms && b.p95_ms <= b.worst_ms);
    const full = JSON.parse(
        invoke(['--bench', '--mode', 'compile-run', '--runs', '3', '--warmup', '0', source]).stdout,
    );
    assert.equal(full.result, 42);
    assert.equal(full.samples_ms.length, 3);
    assert.ok(JSON.parse(invoke(['--dump-cfg', source]).stdout).blocks.length > 0);
    assert.ok(JSON.parse(invoke(['--dump-bytecode', source]).stdout).code.length > 0);
    const passes = JSON.parse(invoke(['--dump-passes', source]).stdout);
    assert.equal(passes.schema_version, 1);
    assert.equal(passes.reused, false);
    assert.equal(passes.passes.length, 7);
    assert.ok(passes.passes.every((pass) => pass.name && pass.before && pass.after && pass.elapsed_us >= 0));
    assert.equal(JSON.parse(invoke(['--dump-passes', '--no-opt', source]).stdout).passes.length, 0);
    assert.equal(JSON.parse(invoke(['--dump-ir', source]).stdout).functions[0].ssa, true);
    const analyses = JSON.parse(invoke(['--dump-analyses', source]).stdout);
    assert.equal(analyses.postdominance_model, 'finite-exit-paths');
    assert.equal(analyses.functions[0].effects.may_trap, false);
    assert.equal(analyses.functions[0].effects.can_discard_call, true);
    const traceFile = join(dir, 'compiler trace.json');
    const trace = JSON.parse(invoke(['--dump-pass-trace', '--out', traceFile, source]).stdout);
    assert.equal(trace.snapshots.length, 7);
    assert.deepEqual(JSON.parse(readFileSync(traceFile, 'utf8')), trace);
    assert.equal(replayTrace(cli, trace).replayed_passes.length, 7);
    const corrupted = structuredClone(trace);
    corrupted.report.passes[2].after_fingerprint = 'fnv1a64:0000000000000000';
    assert.throws(() => replayTrace(cli, corrupted), /Trace content mismatch/);
    const snapshotFile = join(dir, 'raw snapshot.mplxir');
    const snapshotReport = JSON.parse(
        invoke(['--snapshot-ir', '--no-opt', '--out', snapshotFile, source]).stdout,
    );
    assert.equal(snapshotReport.snapshot_version, 1);
    assert.equal(readFileSync(snapshotFile).subarray(0, 8).toString(), 'MPLXIR\r\n');
    assert.equal(
        JSON.parse(invoke(['--replay-pass', '--pass', 'promote-locals-to-ssa', snapshotFile]).stdout)
            .snapshots[0].after_ir.functions[0].ssa,
        true,
    );
    invoke(['--replay-pass', '--pass', 'unknown', snapshotFile], 1);
    invoke(['--replay-pass', snapshotFile], 1);
    invoke(['--snapshot-ir', '--out', source, source], 1);
    invoke(['--dump-pass-trace', '--out', source, source], 1);
    const brokenSnapshot = join(dir, 'broken.mplxir');
    writeFileSync(brokenSnapshot, readFileSync(snapshotFile).subarray(0, 30));
    invoke(['--replay-pass', '--pass', 'promote-locals-to-ssa', brokenSnapshot], 1);
    if (jitEnabled) {
        const machine = JSON.parse(invoke(['--dump-machine', source]).stdout);
        assert.equal(machine.machine_ir.allocation_scope, 'basic-block');
        assert.ok(machine.machine_ir.blocks.length > 0);
        assert.ok(machine.machine_ir.values.length > 0);
        assert.ok(machine.allocation.register_writes > 0);
        assert.ok(machine.native_bytes > 0);
    } else {
        assert.match(invoke(['--dump-machine', source], 1).stderr, /JIT-enabled build/);
    }
    invoke(['--bench', '--runs', '0', source], 2);
    invoke(['--run', '--max-steps', 'oops', source], 2);
    invoke(['--run', '--typo', source], 2);
    invoke(['--run', '--jit', 'bad', source], 2);
    invoke(['--run', source, '--max-steps'], 2);
    writeFileSync(source, 'fn sum(a:i32,b:bool)->i64{return i64(a)+i64(b);}fn main()->i64{return 42;}');
    const module = join(dir, 'nested', 'program.mplxb');
    const built = JSON.parse(invoke(['--build', source, '--out', module]).stdout);
    assert.equal(built.format_version, '1.0');
    assert.equal(built.bytes, readFileSync(module).length);
    const original = readFileSync(module);
    invoke(['--build', source, '--out', module]);
    assert.deepEqual(readFileSync(module), original);
    const info = JSON.parse(invoke(['--inspect-module', module]).stdout);
    assert.deepEqual(info.exports[0].parameters, ['i32', 'bool']);
    assert.equal(info.exports[0].return_type, 'i64');
    for (const jit of jitEnabled ? ['off', 'on'] : ['off']) {
        assert.equal(
            invoke([
                '--run-module',
                module,
                '--entry',
                'sum',
                '--arg',
                '41',
                '--arg',
                '1',
                '--jit',
                jit,
                ...(jitEnabled ? ['--jit-verify'] : []),
                '--no-runfile',
            ]).stdout.trim(),
            'Result: 42',
        );
        invoke(
            ['--run-module', module, '--entry', 'sum', '--arg', '2147483648', '--arg', '0', '--no-runfile'],
            1,
        );
        invoke(['--run-module', module, '--entry', 'sum', '--arg', '1', '--arg', '2', '--no-runfile'], 1);
    }
    if (!jitEnabled) invoke(['--run-module', module, '--jit', 'on', '--no-runfile'], 2);
    assert.equal(
        invoke([
            '--run',
            source,
            '--entry',
            'sum',
            '--arg',
            '-2',
            '--arg',
            '1',
            '--no-runfile',
        ]).stdout.trim(),
        'Result: -1',
    );
    invoke(['--run-module', module, '--arg', '9223372036854775808', '--no-runfile'], 2);
    invoke(['--build', source, '--out', source], 1); // Never replace the source with binary output.
    assert.match(readFileSync(source, 'utf8'), /fn sum/);
    writeFileSync(source, 'fn main()->i64{return @;}');
    invoke(['--build', source, '--out', module], 1);
    assert.deepEqual(readFileSync(module), original);
    const corrupt = Buffer.from(original);
    corrupt[corrupt.length - 1] ^= 1;
    writeFileSync(module, corrupt);
    assert.match(invoke(['--run-module', module, '--no-runfile'], 1).stderr, /checksum/);
    writeFileSync(module, original.subarray(0, 100));
    invoke(['--inspect-module', module], 1);
    const dependency = join(dir, 'math.mplx');
    const dependencySource = 'export fn sum(a:i64,b:i64)->i64{return a+b;}';
    writeFileSync(dependency, dependencySource);
    writeFileSync(source, 'import "math.mplx" as math;fn main()->i64{return math::sum(20,22);}');
    const workspace = JSON.parse(invoke(['--workspace', source]).stdout);
    assert.equal(workspace.schema_version, 2);
    assert.equal(workspace.files.length, 2);
    assert.deepEqual(workspace.diagnostics, []);
    assert.equal(workspace.dependencies.length, 1);
    assert.equal(invoke(['--run', source, '--no-runfile']).stdout.trim(), 'Result: 42');
    assert.ok(
        JSON.parse(invoke(['--dump-ir', source]).stdout).functions.some(
            (value) => value.name === 'math.mplx::sum',
        ),
    );
    invoke(['--build', source, '--out', dependency], 1);
    assert.equal(readFileSync(dependency, 'utf8'), dependencySource);
    invoke(['--build', source, '--out', module]);
    rmSync(dependency);
    assert.equal(invoke(['--run-module', module, '--no-runfile']).stdout.trim(), 'Result: 42');
    const missingDependency = JSON.parse(invoke(['--check', source], 1).stdout);
    assert.ok(missingDependency.diagnostics.some((value) => value.code === 'MPLX4002' && value.path));
    writeFileSync(source, 'fn main()->i64{return 1/0;}');
    invoke(['--run', '--no-runfile', source], 1);
    writeFileSync(source, 'fn main()->i64{while(true){}return 0;}');
    const bounded = invoke(['--run', '--max-steps', '25', '--no-runfile', source], 1);
    assert.match(bounded.stderr, /instruction limit/);
    writeFileSync(source, 'fn main()->i64{return @;}');
    const checked = JSON.parse(invoke(['--check', source], 1).stdout);
    assert.ok(checked.diagnostics.length > 0);
    assert.equal(checked.diagnostics[0].line, 1);
    invoke(['--run', '--no-runfile', source], 1);
    const missing = JSON.parse(invoke(['--check', join(dir, 'missing"file.mplx')], 1).stdout);
    assert.ok(missing.diagnostics.length > 0);
    console.log('CLI source/module integration passed');
} finally {
    rmSync(dir, { recursive: true, force: true });
}
