import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { tmpdir } from 'node:os';
import { TextDocument } from 'vscode-languageserver-textdocument';
import {
    createMessageConnection,
    StreamMessageReader,
    StreamMessageWriter,
    IPCMessageReader,
    IPCMessageWriter,
} from 'vscode-jsonrpc/node.js';
const base = dirname(fileURLToPath(import.meta.url)),
    root = resolve(base, '../../..');
const cli = resolve(
    process.argv[2] ?? resolve(root, 'build/dev/bin/mplx' + (process.platform === 'win32' ? '.exe' : '')),
);
const ipc = process.argv[3] === '--ipc';
const server = process.env.MPLX_LSP_SERVER ?? resolve(base, '../dist/server.js');
const child = spawn(process.execPath, [server, ipc ? '--node-ipc' : '--stdio'], {
    stdio: ipc ? ['pipe', 'pipe', 'pipe', 'ipc'] : 'pipe',
    windowsHide: true,
});
let stderr = '';
child.stderr.on('data', (d) => (stderr += d.toString()));
const connection = createMessageConnection(
    ipc ? new IPCMessageReader(child) : new StreamMessageReader(child.stdout),
    ipc ? new IPCMessageWriter(child) : new StreamMessageWriter(child.stdin),
);
const diagnostics = [];
connection.onNotification('textDocument/publishDiagnostics', (d) => diagnostics.push(d));
connection.listen();
const uri = pathToFileURL(join(tmpdir(), 'mplx-lsp-' + process.pid + '.mplx')).href;
let version = 0,
    text = '';
const waiters = [];
connection.onNotification('window/logMessage', () => {});
async function request(method, params) {
    return await Promise.race([
        connection.sendRequest(method, params),
        new Promise((_, reject) => {
            const timer = setTimeout(
                () => reject(new Error('LSP timeout: ' + method + '\n' + stderr)),
                15000,
            );
            timer.unref();
            waiters.push(timer);
        }),
    ]);
}
function position(needle, offset = 0) {
    const start = text.indexOf(needle);
    assert.ok(start >= 0, 'missing test needle ' + needle);
    return TextDocument.create(uri, 'mplx', version, text).positionAt(start + offset);
}
function documentParams(position) {
    return { textDocument: { uri }, position };
}
async function open(source) {
    text = source;
    version = 1;
    await connection.sendNotification('textDocument/didOpen', {
        textDocument: { uri, languageId: 'mplx', version, text },
    });
    await request('textDocument/documentSymbol', { textDocument: { uri } });
}
async function change(source) {
    text = source;
    ++version;
    await connection.sendNotification('textDocument/didChange', {
        textDocument: { uri, version },
        contentChanges: [{ text }],
    });
    await request('textDocument/documentSymbol', { textDocument: { uri } });
}
function latestDiagnostic() {
    return diagnostics.filter((d) => d.uri === uri).at(-1);
}
try {
    const initialized = await request('initialize', {
        processId: process.pid,
        capabilities: { textDocument: { publishDiagnostics: { versionSupport: true } } },
        workspaceFolders: [{ uri: pathToFileURL(root).href, name: 'MPLX' }],
        initializationOptions: { cliPath: cli, debounceMs: 0 },
    });
    assert.equal(initialized.capabilities.renameProvider.prepareProvider, true);
    await connection.sendNotification('initialized', {});
    await open(
        '// 😀 x in a comment\r\nfn add(a:i64,b:i64)->i64{return a+b;}\r\nfn main()->i64{\r\n let x=1;\r\n if(true){\r\n  let x=x+1;\r\n  x=x+2;\r\n }\r\n return add(x,2);\r\n}',
    );
    assert.deepEqual(latestDiagnostic().diagnostics, []);
    const outer = position('let x=1', 4),
        inner = position('let x=x+1', 4),
        initializer = position('let x=x+1', 6),
        innerUse = position('x=x+2', 0);
    assert.deepEqual(
        (await request('textDocument/definition', documentParams(initializer)))[0].range.start,
        outer,
    );
    assert.deepEqual(
        (await request('textDocument/definition', documentParams(innerUse)))[0].range.start,
        inner,
    );
    const hover = await request('textDocument/hover', documentParams(innerUse));
    assert.match(hover.contents.value, /local x: i64/);
    const refs = await request('textDocument/references', {
        ...documentParams(outer),
        context: { includeDeclaration: true },
    });
    assert.equal(refs.length, 3);
    assert.equal(
        refs.some((r) => r.range.start.line === 0),
        false,
    );
    const innerRefs = await request('textDocument/references', {
        ...documentParams(inner),
        context: { includeDeclaration: true },
    });
    assert.equal(innerRefs.length, 3);
    const edit = await request('textDocument/rename', { ...documentParams(outer), newName: 'total' });
    assert.equal(edit.documentChanges[0].textDocument.version, version);
    assert.equal(edit.documentChanges[0].edits.length, 3);
    assert.equal(
        edit.documentChanges[0].edits.some((e) => e.range.start.line === 6),
        false,
    );
    const prepared = await request('textDocument/prepareRename', documentParams(innerUse));
    assert.equal(prepared.placeholder, 'x');
    const complete = await request('textDocument/completion', documentParams(innerUse));
    assert.equal(complete.filter((c) => c.label === 'x').length, 1);
    const outline = await request('textDocument/documentSymbol', { textDocument: { uri } });
    assert.deepEqual(
        outline.map((s) => s.name),
        ['add', 'main'],
    );
    assert.ok(outline[1].children.filter((s) => s.name === 'x').length === 2);
    const workspace = await request('workspace/symbol', { query: 'add' });
    assert.equal(workspace[0].name, 'add');
    const tokens = await request('textDocument/semanticTokens/full', { textDocument: { uri } });
    assert.ok(tokens.data.length > 0 && tokens.data.length % 5 === 0);
    await assert.rejects(
        request('textDocument/rename', { ...documentParams(outer), newName: 'while' }),
        /nonreserved/,
    );
    await change('fn main()->i64{let x=1;let total=2;return x+total;}');
    await assert.rejects(
        request('textDocument/rename', { ...documentParams(position('let x=1', 4)), newName: 'total' }),
        /duplicate/,
    );
    await change('fn main()->i64{let outer=1;if(true){let inner=2;return outer+inner;}return outer;}');
    await assert.rejects(
        request('textDocument/rename', { ...documentParams(position('let outer=1', 4)), newName: 'inner' }),
        /capture|binding/,
    );
    await assert.rejects(
        request('textDocument/rename', { ...documentParams(position('let inner=2', 4)), newName: 'outer' }),
        /capture|binding/,
    );
    await change('fn main()->i64{let x=1;if(true){let x=x+1;return x;}return x;}');
    const beforeBinding = position('let x=x+1', 6);
    const visible = await request('textDocument/completion', documentParams(beforeBinding));
    assert.equal(visible.filter((c) => c.label === 'x').length, 1);
    const rename = await request('textDocument/rename', {
        ...documentParams(position('let x=1', 4)),
        newName: 'renamed',
    });
    assert.equal(rename.documentChanges[0].edits.length, 3);
    await change('fn add(a:i64,b:i64)->i64{return a+b;}\nfn main()->i64{return add(1, ');
    const signature = await request(
        'textDocument/signatureHelp',
        documentParams(TextDocument.create(uri, 'mplx', version, text).positionAt(text.length)),
    );
    assert.equal(signature.signatures[0].label, 'add(a: i64, b: i64) -> i64');
    assert.equal(signature.activeParameter, 1);
    await change('// 😀 UTF16 columns\nfn main()->i64{\n return missing;\n}');
    const diagnosed = latestDiagnostic();
    assert.equal(diagnosed.version, version);
    assert.equal(diagnosed.diagnostics[0].code, 'MPLX2006');
    assert.deepEqual(diagnosed.diagnostics[0].range.start, position('missing'));
    assert.equal(
        diagnosed.diagnostics[0].range.end.character - diagnosed.diagnostics[0].range.start.character,
        7,
    );
    const latestVersion = version + 3;
    const rapid = [
        'fn main()->i64{return missing;}',
        'fn main()->i64{return true+1;}',
        'fn main()->i64{return 42;}',
    ];
    for (const source of rapid) {
        text = source;
        ++version;
        await connection.sendNotification('textDocument/didChange', {
            textDocument: { uri, version },
            contentChanges: [{ text }],
        });
    }
    await request('textDocument/documentSymbol', { textDocument: { uri } });
    assert.equal(latestDiagnostic().version, latestVersion);
    assert.deepEqual(latestDiagnostic().diagnostics, []);
    await connection.sendNotification('textDocument/didClose', { textDocument: { uri } });
    await request('textDocument/documentSymbol', { textDocument: { uri } });
    assert.deepEqual(latestDiagnostic().diagnostics, []);
    console.log(
        'LSP protocol: shadowing, source ranges, capture-safe rename, completion, signatures, semantic tokens, rapid edits and close passed.',
    );
} finally {
    for (const timer of waiters) clearTimeout(timer);
    try {
        await connection.sendRequest('shutdown');
        await connection.sendNotification('exit');
    } catch {}
    connection.dispose();
    child.kill();
}
