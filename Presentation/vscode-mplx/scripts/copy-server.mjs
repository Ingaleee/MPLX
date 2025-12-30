import { cpSync, existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, dirname } from 'node:path';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const source = resolve(root, '../Mplx.Lsp/dist');
if (!existsSync(resolve(source, 'server.js')))
    throw new Error('Build Presentation/Mplx.Lsp before building the extension.');
mkdirSync(resolve(root, 'server'), { recursive: true });
cpSync(source, resolve(root, 'server'), { recursive: true });
writeFileSync(resolve(root, 'server/package.json'), JSON.stringify({ type: 'module' }) + '\n');
