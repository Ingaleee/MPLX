import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, mkdirSync, writeFileSync, existsSync } from 'node:fs';
import { cpus, platform, arch, release } from 'node:os';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const [binary, output, runs = '50'] = process.argv.slice(2);
if (!binary || !output || !/^\d+$/u.test(runs) || Number(runs) < 3 || Number(runs) > 10000) {
    throw new Error(
        'Usage: node scripts/compiler-benchmark.mjs <mplx-compiler-bench> <report.json> [runs:3..10000]',
    );
}
const executable = resolve(binary);
const result = spawnSync(executable, [runs], {
    cwd: root,
    encoding: 'utf8',
    timeout: 300000,
    maxBuffer: 64 * 1024 * 1024,
});
if (result.error || result.status !== 0) {
    throw new Error(result.error?.message ?? result.stderr ?? 'compiler benchmark failed');
}
const hash = (contents) => createHash('sha256').update(contents).digest('hex');
const git = (...args) => {
    const result = spawnSync('git', ['-c', `safe.directory=${root}`, ...args], {
        cwd: root,
        encoding: 'utf8',
    });
    if (result.error || result.status !== 0) throw new Error(result.error?.message ?? result.stderr);
    return result.stdout;
};
const sourceFiles = [
    ...new Set(git('ls-files', '--cached', '--others', '--exclude-standard', '-z').split('\0')),
]
    .filter(
        (file) =>
            /^(?:Application\/(?:Jit|mplx-compiler|mplx-vm)|Domain\/mplx-lang)\/.*\.(?:cpp|hpp)$/u.test(
                file,
            ) && existsSync(resolve(root, file)),
    )
    .sort();
const sourceDigest = createHash('sha256');
for (const file of sourceFiles) {
    const contents = readFileSync(resolve(root, file));
    sourceDigest.update(`${file}\0${contents.length}\0`).update(contents);
}
const benchmark = JSON.parse(result.stdout);
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
    git_head: git('rev-parse', 'HEAD').trim(),
    working_tree_dirty: git('status', '--porcelain').trim().length > 0,
    binary_sha256: hash(readFileSync(executable)),
    native_source_sha256: sourceDigest.digest('hex'),
    command: { binary, runs: Number(runs), warmup: 3 },
    benchmark,
};
mkdirSync(dirname(resolve(output)), { recursive: true });
writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
console.log(
    `Saved ${resolve(output)}; ${benchmark.workloads.length} workloads with ${runs} samples per mode.`,
);
