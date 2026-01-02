import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
const [version, output, ...paths] = process.argv.slice(2);
if (!version || !output || !paths.length)
    throw new Error('Usage: artifact-manifest.mjs VERSION OUTPUT ARTIFACT...');
const artifacts = paths.map((path) => {
    const bytes = readFileSync(path);
    return { path, bytes: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
});
writeFileSync(
    output,
    JSON.stringify({ version, created_at: new Date().toISOString(), artifacts }, null, 2) + '\n',
);
