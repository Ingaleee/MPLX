import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { TextDocument } from 'vscode-languageserver-textdocument';
import {
    createMessageConnection,
    StreamMessageReader,
    StreamMessageWriter,
    IPCMessageReader,
    IPCMessageWriter,
} from 'vscode-jsonrpc/node.js';

const root = mkdtempSync(join(tmpdir(), 'mplx-workspace-'));
const cli = resolve(process.argv[2]);
const ipc = process.argv[3] === '--ipc';
const server = process.env.MPLX_LSP_SERVER ?? fileURLToPath(new URL('../dist/server.js', import.meta.url));
const child = spawn(process.execPath, [server, ipc ? '--node-ipc' : '--stdio'], {
    stdio: ipc ? ['pipe', 'pipe', 'pipe', 'ipc'] : 'pipe',
    windowsHide: true,
});
let stderr = '';
child.stderr.on('data', (data) => (stderr += data.toString()));
const connection = createMessageConnection(
    ipc ? new IPCMessageReader(child) : new StreamMessageReader(child.stdout),
    ipc ? new IPCMessageWriter(child) : new StreamMessageWriter(child.stdin),
);
const diagnostics = new Map();
connection.onNotification('textDocument/publishDiagnostics', (value) => diagnostics.set(value.uri, value));
connection.onNotification('window/logMessage', () => {});
connection.listen();
const texts = new Map();
const versions = new Map();
function uri(name) {
    return pathToFileURL(join(root, name)).href;
}
function source(name, text) {
    writeFileSync(join(root, name), text);
    texts.set(name, text);
}
async function request(method, params) {
    let timer;
    try {
        return await Promise.race([
            connection.sendRequest(method, params),
            new Promise((_, reject) => {
                timer = setTimeout(() => reject(new Error(method + ' timed out\n' + stderr)), 15000);
            }),
        ]);
    } finally {
        clearTimeout(timer);
    }
}
function position(name, needle, offset = 0) {
    const text = texts.get(name);
    const start = text.indexOf(needle);
    assert.ok(start >= 0, needle);
    return TextDocument.create(uri(name), 'mplx', versions.get(name) ?? 0, text).positionAt(start + offset);
}
function params(name, needle, offset = 0) {
    return { textDocument: { uri: uri(name) }, position: position(name, needle, offset) };
}
async function open(name) {
    versions.set(name, 1);
    await connection.sendNotification('textDocument/didOpen', {
        textDocument: { uri: uri(name), languageId: 'mplx', version: 1, text: texts.get(name) },
    });
    await request('textDocument/documentSymbol', { textDocument: { uri: uri(name) } });
}
async function change(name, text) {
    const version = (versions.get(name) ?? 0) + 1;
    versions.set(name, version);
    texts.set(name, text);
    await connection.sendNotification('textDocument/didChange', {
        textDocument: { uri: uri(name), version },
        contentChanges: [{ text }],
    });
    await request('textDocument/documentSymbol', { textDocument: { uri: uri(name) } });
}
try {
    source(
        'math.mplx',
        '// 😀 exported symbol with UTF8\r\nexport fn sum(a:i64,b:i64)->i64{return a+b;}\r\nfn hidden()->i64{return 7;}',
    );
    source('main.mplx', 'import "math.mplx" as math;\nfn main()->i64{return math::sum(20,22);}');
    source('second.mplx', 'import "math.mplx" as m;\nfn other()->i64{return m::sum(1,2);}');
    await request('initialize', {
        processId: process.pid,
        capabilities: {},
        workspaceFolders: [{ uri: pathToFileURL(root).href, name: 'modules' }],
        initializationOptions: { cliPath: cli, debounceMs: 0 },
    });
    await connection.sendNotification('initialized', {});
    await open('main.mplx');
    assert.deepEqual(diagnostics.get(uri('main.mplx')).diagnostics, []);
    const definition = await request('textDocument/definition', params('main.mplx', 'sum'));
    assert.equal(definition[0].uri, uri('math.mplx'));
    assert.deepEqual(definition[0].range.start, position('math.mplx', 'sum'));
    const importDefinition = await request('textDocument/definition', params('main.mplx', '"math.mplx"', 3));
    assert.equal(importDefinition[0].uri, uri('math.mplx'));
    const refs = await request('textDocument/references', {
        ...params('main.mplx', 'sum'),
        context: { includeDeclaration: true },
    });
    assert.equal(refs.length, 3);
    assert.deepEqual(
        new Set(refs.map((value) => value.uri)),
        new Set(['main.mplx', 'math.mplx', 'second.mplx'].map(uri)),
    );
    const symbols = await request('workspace/symbol', { query: 'sum' });
    assert.equal(symbols.length, 1);
    assert.equal(symbols[0].location.uri, uri('math.mplx'));
    const documentSymbols = await request('textDocument/documentSymbol', {
        textDocument: { uri: uri('main.mplx') },
    });
    assert.deepEqual(
        documentSymbols.map((value) => value.name),
        ['main'],
    );
    const completion = await request('textDocument/completion', params('main.mplx', 'return', 7));
    assert.ok(completion.some((value) => value.label === 'math::sum'));
    assert.ok(!completion.some((value) => value.label === 'math::hidden'));
    const qualified = await request('textDocument/completion', params('main.mplx', 'sum'));
    assert.equal(qualified.find((value) => value.label === 'sum').insertText, 'sum');
    const hover = await request('textDocument/hover', params('main.mplx', 'sum'));
    assert.match(hover.contents.value, /math::sum\(a: i64, b: i64\)/);
    const signature = await request('textDocument/signatureHelp', params('main.mplx', '22'));
    assert.equal(signature.signatures[0].parameters.length, 2);

    const rename = await request('textDocument/rename', {
        ...params('main.mplx', 'sum'),
        newName: 'total',
    });
    assert.equal(rename.documentChanges.length, 3);
    assert.equal(
        rename.documentChanges.find((value) => value.textDocument.uri === uri('main.mplx')).textDocument
            .version,
        1,
    );
    assert.equal(
        rename.documentChanges.find((value) => value.textDocument.uri === uri('second.mplx')).textDocument
            .version,
        null,
    );
    for (const file of rename.documentChanges) {
        const name = ['main.mplx', 'math.mplx', 'second.mplx'].find(
            (value) => uri(value) === file.textDocument.uri,
        );
        for (const edit of file.edits) {
            const document = TextDocument.create(file.textDocument.uri, 'mplx', 0, texts.get(name));
            assert.equal(document.getText(edit.range), 'sum');
        }
    }
    await assert.rejects(
        request('textDocument/rename', {
            ...params('main.mplx', 'sum'),
            newName: 'hidden',
        }),
        /diagnostics|duplicate/,
    );
    await assert.rejects(
        request('textDocument/rename', {
            ...params('main.mplx', 'sum'),
            newName: 'import',
        }),
        /nonreserved/,
    );

    await open('math.mplx');
    await change('math.mplx', 'export fn sum(a:i64)->i64{return a;}');
    await request('textDocument/documentSymbol', { textDocument: { uri: uri('main.mplx') } });
    assert.ok(diagnostics.get(uri('main.mplx')).diagnostics.some((value) => value.code === 'MPLX2018'));
    await change('math.mplx', '// shifted declaration\n\nexport fn sum(a:i64,b:i64)->i64{return a+b+1;}');
    const moved = await request('textDocument/definition', params('main.mplx', 'sum'));
    assert.equal(moved[0].range.start.line, 2);
    await request('textDocument/documentSymbol', { textDocument: { uri: uri('main.mplx') } });
    assert.deepEqual(diagnostics.get(uri('main.mplx')).diagnostics, []);
    const rapid = [
        'export fn sum(a:i64)->i64{return a;}',
        'export fn sum(a:i64,b:i64)->i64{return true+1;}',
        'export fn sum(a:i64,b:i64)->i64{return a+b;}',
    ];
    for (const text of rapid) {
        texts.set('math.mplx', text);
        versions.set('math.mplx', versions.get('math.mplx') + 1);
        await connection.sendNotification('textDocument/didChange', {
            textDocument: { uri: uri('math.mplx'), version: versions.get('math.mplx') },
            contentChanges: [{ text }],
        });
    }
    await request('textDocument/documentSymbol', { textDocument: { uri: uri('main.mplx') } });
    assert.deepEqual(diagnostics.get(uri('main.mplx')).diagnostics, []);
    assert.equal(diagnostics.get(uri('math.mplx')).version, versions.get('math.mplx'));

    // Closing an unsaved dependency restores its disk version.
    await connection.sendNotification('textDocument/didClose', { textDocument: { uri: uri('math.mplx') } });
    const disk = await request('textDocument/definition', params('main.mplx', 'sum'));
    assert.equal(disk[0].range.start.line, 1);
    source('math.mplx', 'export fn sum(a:i64)->i64{return a;}');
    await connection.sendNotification('workspace/didChangeWatchedFiles', {
        changes: [{ uri: uri('math.mplx'), type: 2 }],
    });
    await request('textDocument/documentSymbol', { textDocument: { uri: uri('main.mplx') } });
    assert.ok(diagnostics.get(uri('main.mplx')).diagnostics.some((value) => value.code === 'MPLX2018'));
    source('math.mplx', 'export fn sum(a:i64,b:i64)->i64{return a+b;}');
    await connection.sendNotification('workspace/didChangeWatchedFiles', {
        changes: [{ uri: uri('math.mplx'), type: 2 }],
    });
    await request('textDocument/documentSymbol', { textDocument: { uri: uri('main.mplx') } });
    assert.deepEqual(diagnostics.get(uri('main.mplx')).diagnostics, []);

    source('created.mplx', 'export fn newlyIndexed()->i64{return 5;}');
    await connection.sendNotification('workspace/didChangeWatchedFiles', {
        changes: [{ uri: uri('created.mplx'), type: 1 }],
    });
    assert.equal((await request('workspace/symbol', { query: 'newlyIndexed' })).length, 1);
    rmSync(join(root, 'created.mplx'));
    await connection.sendNotification('workspace/didChangeWatchedFiles', {
        changes: [{ uri: uri('created.mplx'), type: 3 }],
    });
    assert.equal((await request('workspace/symbol', { query: 'newlyIndexed' })).length, 0);

    // Existing Unicode filenames must have the same identity in the compiler,
    // editor snapshots and overlays. Windows folds ASCII only in the native key.
    source('Математика.mplx', 'export fn unicodeValue()->i64{return 42;}');
    source(
        'unicode-main.mplx',
        'import "Математика.mplx" as u;fn unicodeMain()->i64{return u::unicodeValue();}',
    );
    await connection.sendNotification('workspace/didChangeWatchedFiles', {
        changes: [
            { uri: uri('Математика.mplx'), type: 1 },
            { uri: uri('unicode-main.mplx'), type: 1 },
        ],
    });
    await open('unicode-main.mplx');
    const unicodeDefinition = await request(
        'textDocument/definition',
        params('unicode-main.mplx', 'unicodeValue'),
    );
    assert.equal(unicodeDefinition[0].uri, uri('Математика.mplx'));
    await open('Математика.mplx');
    const unicodeSymbols = await request('textDocument/documentSymbol', {
        textDocument: { uri: uri('Математика.mplx') },
    });
    assert.deepEqual(
        unicodeSymbols.map((value) => value.name),
        ['unicodeValue'],
    );
    assert.equal(diagnostics.get(uri('Математика.mplx')).version, 1);
    await change('Математика.mplx', '// unsaved location\nexport fn unicodeValue()->i64{return 43;}');
    const unicodeOverlay = await request(
        'textDocument/definition',
        params('unicode-main.mplx', 'unicodeValue'),
    );
    assert.equal(unicodeOverlay[0].range.start.line, 1);
    const unicodeRename = await request('textDocument/rename', {
        ...params('unicode-main.mplx', 'unicodeValue'),
        newName: 'renamedUnicode',
    });
    assert.equal(unicodeRename.documentChanges.length, 2);
    assert.equal(
        unicodeRename.documentChanges.find((value) => value.textDocument.uri === uri('Математика.mplx'))
            .textDocument.version,
        versions.get('Математика.mplx'),
    );
    const stats = await request('mplx/workspaceStatistics', {});
    assert.ok(stats.some((value) => value.semantic_hits > 0));
    const finalRename = await request('textDocument/rename', {
        ...params('main.mplx', 'sum'),
        newName: 'total',
    });
    for (const change of finalRename.documentChanges) {
        const name = ['main.mplx', 'math.mplx', 'second.mplx'].find(
            (value) => uri(value) === change.textDocument.uri,
        );
        const document = TextDocument.create(change.textDocument.uri, 'mplx', 0, texts.get(name));
        source(name, TextDocument.applyEdits(document, change.edits));
    }
    const executed = spawnSync(cli, ['--run', join(root, 'main.mplx'), '--jit', 'off', '--no-runfile'], {
        cwd: root,
        encoding: 'utf8',
        timeout: 10000,
    });
    assert.equal(executed.status, 0, executed.stderr);
    assert.equal(executed.stdout.trim(), 'Result: 42');
    console.log(
        'Cross-file LSP: disk index, definitions/imports, references, transactional rename, overlays, invalidation and stale rejection passed.',
    );
} finally {
    try {
        await connection.sendRequest('shutdown');
        await connection.sendNotification('exit');
    } catch {}
    connection.dispose();
    child.kill();
    rmSync(root, { recursive: true, force: true });
}
