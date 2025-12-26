import {
    createConnection,
    ProposedFeatures,
    TextDocuments,
    TextDocumentSyncKind,
    DiagnosticSeverity,
    MarkupKind,
    CompletionItemKind,
    SymbolKind,
    ResponseError,
    ErrorCodes,
    type CancellationToken,
    type CompletionItem,
    type Diagnostic,
    type DocumentSymbol,
    type Hover,
    type Location,
    type Position,
    type SemanticTokens,
    type SignatureHelp,
} from 'vscode-languageserver/node.js';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { existsSync } from 'node:fs';
import { resolve, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CompilerClient } from './compiler-client.js';
import { Snapshot } from './snapshot.js';
import { WorkspaceIndex } from './workspace-index.js';
import { signature, type Symbol } from './model.js';
const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
let root = process.cwd(),
    explicitPath: string | undefined,
    compiler: CompilerClient | undefined,
    epoch = 0;
let debounce = 80;
let workspace: WorkspaceIndex | undefined;
const snapshots = new Map<string, Snapshot>();
const jobs = new Map<
    string,
    {
        version: number;
        epoch: number;
        promise: Promise<Snapshot | null>;
    }
>();
const scheduled = new Map<string, ReturnType<typeof setTimeout>>();
const legend = {
    tokenTypes: ['function', 'parameter', 'variable'],
    tokenModifiers: ['declaration', 'modification'],
};
function object(value: unknown): Record<string, unknown> | undefined {
    return value !== null && typeof value === 'object' ? (value as Record<string, unknown>) : undefined;
}
function cliPath(): string {
    const suffix = process.platform === 'win32' ? '.exe' : '';
    if (explicitPath) {
        const path = isAbsolute(explicitPath) ? explicitPath : resolve(root, explicitPath);
        return !existsSync(path) && suffix && !path.endsWith(suffix) ? path + suffix : path;
    }
    const candidates = ['dev', 'dev-msvc', 'jit', 'release'].map((p) =>
        resolve(root, 'build', p, 'bin', 'mplx' + suffix),
    );
    return candidates.find((p) => existsSync(p)) ?? candidates[0]!;
}
function resetCompiler(): void {
    ++epoch;
    compiler?.dispose();
    compiler = new CompilerClient(cliPath());
    snapshots.clear();
    jobs.clear();
    workspace = new WorkspaceIndex(
        root,
        compiler,
        () => documents.all(),
        (project, files) => {
            for (const file of files.values()) {
                const current = documents.get(file.document.uri);
                if (!current) {
                    continue;
                }
                const path = workspace?.path(file.document.uri);
                const diagnostics = project.diagnostics.filter((d) => d.path === path);
                connection.sendDiagnostics({
                    uri: file.document.uri,
                    version: current.version,
                    diagnostics: diagnostics.map((d) => diagnostic(file, d)),
                });
            }
        },
    );
}
connection.onInitialize((params) => {
    const folder = params.workspaceFolders?.[0]?.uri ?? params.rootUri;
    if (folder) {
        try {
            root = fileURLToPath(folder);
        } catch {}
    }
    const options = object(params.initializationOptions);
    explicitPath =
        typeof options?.cliPath === 'string' && options.cliPath.length
            ? options.cliPath
            : process.env.MPLX_CLI;
    if (typeof options?.debounceMs === 'number') {
        debounce = Math.max(0, Math.min(options.debounceMs, 1000));
    }
    resetCompiler();
    return {
        capabilities: {
            textDocumentSync: TextDocumentSyncKind.Incremental,
            hoverProvider: true,
            definitionProvider: true,
            referencesProvider: true,
            renameProvider: {
                prepareProvider: true,
            },
            completionProvider: {
                triggerCharacters: [':'],
            },
            signatureHelpProvider: {
                triggerCharacters: ['(', ','],
                retriggerCharacters: [','],
            },
            documentSymbolProvider: true,
            workspaceSymbolProvider: true,
            semanticTokensProvider: {
                legend,
                full: true,
            },
        },
        serverInfo: {
            name: 'MPLX compiler language server',
            version: '0.6.0',
        },
    };
});
function diagnostic(snapshot: Snapshot, d: Snapshot['analysis']['diagnostics'][number]): Diagnostic {
    return {
        code: d.code,
        message: d.message,
        range: snapshot.range(d.range),
        severity: DiagnosticSeverity.Error,
        source: 'mplx',
    };
}
function analyze(document: TextDocument): Promise<Snapshot | null> {
    const uri = document.uri,
        version = document.version,
        activeEpoch = epoch;
    const old = jobs.get(uri);
    if (old && old.version === version && old.epoch === activeEpoch) {
        return old.promise;
    }
    const immutable = TextDocument.create(uri, document.languageId, version, document.getText());
    if (workspace?.path(uri)) {
        const promise = workspace.snapshot(uri).catch((error: unknown) => {
            if (documents.get(uri)?.version === version && epoch === activeEpoch) {
                connection.sendDiagnostics({
                    uri,
                    version,
                    diagnostics: [
                        {
                            code: 'MPLX_SERVICE',
                            message: error instanceof Error ? error.message : String(error),
                            severity: DiagnosticSeverity.Error,
                            source: 'mplx',
                            range: {
                                start: {
                                    line: 0,
                                    character: 0,
                                },
                                end: {
                                    line: 0,
                                    character: 0,
                                },
                            },
                        },
                    ],
                });
            }
            return null;
        });
        return promise;
    }
    const promise = compiler!
        .analyze(immutable.getText())
        .then((result) => {
            const current = documents.get(uri);
            if (!current || current.version !== version || epoch !== activeEpoch) {
                return null;
            }
            const snapshot = new Snapshot(immutable, result);
            snapshots.set(uri, snapshot);
            connection.sendDiagnostics({
                uri,
                version,
                diagnostics: result.diagnostics.map((d) => diagnostic(snapshot, d)),
            });
            return snapshot;
        })
        .catch((error: unknown) => {
            if (documents.get(uri)?.version === version && epoch === activeEpoch) {
                const message = error instanceof Error ? error.message : String(error);
                connection.sendDiagnostics({
                    uri,
                    version,
                    diagnostics: [
                        {
                            code: 'MPLX_SERVICE',
                            message: 'Compiler service unavailable: ' + message,
                            severity: DiagnosticSeverity.Error,
                            source: 'mplx',
                            range: {
                                start: {
                                    line: 0,
                                    character: 0,
                                },
                                end: {
                                    line: 0,
                                    character: 0,
                                },
                            },
                        },
                    ],
                });
            }
            return null;
        })
        .finally(() => {
            if (jobs.get(uri)?.promise === promise) {
                jobs.delete(uri);
            }
        });
    jobs.set(uri, {
        version,
        epoch: activeEpoch,
        promise,
    });
    return promise;
}
async function snapshot(uri: string, token?: CancellationToken): Promise<Snapshot | null> {
    for (;;) {
        if (token?.isCancellationRequested) {
            return null;
        }
        const document = documents.get(uri);
        if (!document) {
            return null;
        }
        const requestedVersion = document.version;
        if (workspace?.path(uri)) {
            const latest = await analyze(document);
            if (token?.isCancellationRequested) {
                return null;
            }
            if (documents.get(uri)?.version === requestedVersion) {
                return latest;
            }
            continue;
        }
        const cached = snapshots.get(uri);
        if (cached?.document.version === document.version) {
            return cached;
        }
        const latest = await analyze(document);
        if (token?.isCancellationRequested) {
            return null;
        }
        const current = documents.get(uri);
        if (!current) {
            return null;
        }
        if (current.version === requestedVersion) {
            return latest;
        }
    }
}
documents.onDidChangeContent((event) => {
    const uri = event.document.uri;
    snapshots.delete(uri);
    workspace?.invalidate();
    const prior = scheduled.get(uri);
    if (prior) {
        clearTimeout(prior);
    }
    scheduled.set(
        uri,
        setTimeout(() => {
            scheduled.delete(uri);
            for (const document of documents.all()) void analyze(document);
        }, debounce),
    );
});
documents.onDidClose((event) => {
    const uri = event.document.uri;
    const timer = scheduled.get(uri);
    if (timer) {
        clearTimeout(timer);
    }
    scheduled.delete(uri);
    snapshots.delete(uri);
    jobs.delete(uri);
    workspace?.invalidate();
    connection.sendDiagnostics({
        uri,
        diagnostics: [],
    });
    for (const document of documents.all()) void analyze(document);
});
connection.onDidChangeWatchedFiles(() => {
    workspace?.invalidate(true);
    for (const document of documents.all()) void analyze(document);
});
connection.onDidChangeConfiguration((event) => {
    const settings = object(event.settings),
        options = object(settings?.mplx);
    if (options && typeof options.cliPath === 'string') {
        explicitPath = options.cliPath || undefined;
        resetCompiler();
        for (const doc of documents.all()) void analyze(doc);
    }
});
connection.onHover(async (params, token): Promise<Hover | null> => {
    const current = await snapshot(params.textDocument.uri, token);
    if (!current) {
        return null;
    }
    const occurrence = current.occurrence(params.position),
        symbol = current.symbol(params.position);
    return occurrence && symbol
        ? {
              contents: {
                  kind: MarkupKind.Markdown,
                  value: '```mplx\n' + signature(symbol) + '\n```',
              },
              range: current.range(occurrence.range),
          }
        : null;
});
connection.onDefinition(async (params, token): Promise<Location[] | null> => {
    const current = await snapshot(params.textDocument.uri, token),
        symbol = current?.symbol(params.position);
    if (current && workspace?.path(current.document.uri)) {
        const dependency = workspace.dependency(current, params.position);
        if (dependency) {
            return [
                {
                    uri: workspace.uri(dependency.to),
                    range: {
                        start: {
                            line: 0,
                            character: 0,
                        },
                        end: {
                            line: 0,
                            character: 0,
                        },
                    },
                },
            ];
        }
        const location = symbol ? workspace.definition(current, symbol) : undefined;
        return location ? [location] : null;
    }
    return current && symbol
        ? [
              {
                  uri: current.document.uri,
                  range: current.range(symbol.declaration),
              },
          ]
        : null;
});
connection.onReferences(async (params, token): Promise<Location[]> => {
    const current = await snapshot(params.textDocument.uri, token),
        symbol = current?.symbol(params.position);
    if (!current || !symbol) {
        return [];
    }
    if (workspace?.path(current.document.uri)) {
        return workspace.references(current, symbol, params.context.includeDeclaration, token);
    }
    return current.analysis.semantic.occurrences
        .filter((o) => o.symbol === symbol.id && (params.context.includeDeclaration || !o.declaration))
        .map((o) => ({
            uri: current.document.uri,
            range: current.range(o.range),
        }));
});
connection.onPrepareRename(async (params, token) => {
    const current = await snapshot(params.textDocument.uri, token),
        occurrence = current?.occurrence(params.position);
    if (!current || !occurrence) {
        return null;
    }
    const symbol = current.analysis.semantic.symbols[occurrence.symbol];
    if (!symbol) {
        return null;
    }
    return {
        range: current.range(occurrence.range),
        placeholder: symbol.original_name || symbol.name,
    };
});
connection.onRenameRequest(async (params, token) => {
    const current = await snapshot(params.textDocument.uri, token);
    if (!current) {
        return null;
    }
    try {
        if (workspace?.path(current.document.uri)) {
            return {
                documentChanges: await workspace.rename(current, params.position, params.newName, token),
            };
        }
        const renamed = current.rename(params.position, params.newName);
        return {
            documentChanges: [
                {
                    textDocument: {
                        uri: current.document.uri,
                        version: current.document.version,
                    },
                    edits: renamed.occurrences.map((o) => ({
                        range: current.range(o.range),
                        newText: params.newName,
                    })),
                },
            ],
        };
    } catch (error) {
        throw new ResponseError(
            ErrorCodes.InvalidParams,
            error instanceof Error ? error.message : String(error),
        );
    }
});
function kind(symbol: Symbol): CompletionItemKind {
    return symbol.kind === 'function'
        ? CompletionItemKind.Function
        : symbol.kind === 'parameter'
          ? CompletionItemKind.Variable
          : CompletionItemKind.Variable;
}
connection.onCompletion(async (params, token): Promise<CompletionItem[]> => {
    const current = await snapshot(params.textDocument.uri, token);
    if (!current) {
        return [];
    }
    const prefix = current.document.getText().slice(0, current.document.offsetAt(params.position));
    const namespace = /([A-Za-z_][A-Za-z0-9_]*)::$/.exec(prefix)?.[1];
    if (namespace) {
        return current
            .visible(params.position)
            .filter((symbol) => symbol.imported && symbol.name.startsWith(namespace + '::'))
            .map((symbol) => ({
                label: symbol.original_name || symbol.name,
                kind: kind(symbol),
                detail: signature(symbol),
                insertText: symbol.original_name || symbol.name,
            }));
    }
    return [
        ...current.visible(params.position).map((s) => ({
            label: s.name,
            kind: kind(s),
            detail: signature(s),
            insertText: s.name,
        })),
        ...['fn', 'let', 'return', 'if', 'else', 'while', 'true', 'false', 'import', 'as', 'export'].map(
            (label) => ({
                label,
                kind: CompletionItemKind.Keyword,
            }),
        ),
        ...['i32', 'i64', 'bool'].map((label) => ({
            label,
            kind: CompletionItemKind.TypeParameter,
            detail: 'Checked ' + label + ' conversion / type',
        })),
    ];
});
connection.onSignatureHelp(async (params, token): Promise<SignatureHelp | null> => {
    const current = await snapshot(params.textDocument.uri, token);
    if (!current) {
        return null;
    }
    const offset = current.byteOffset(params.position);
    const call = current.analysis.semantic.calls
        .filter((c) => c.range.start.offset <= offset && offset <= c.range.end.offset)
        .sort((a, b) => b.range.start.offset - a.range.start.offset)[0];
    if (!call) {
        return null;
    }
    const symbol = current.analysis.semantic.symbols[call.symbol];
    if (!symbol) {
        return null;
    }
    let parameter = call.arguments.findIndex((a) => a.start.offset <= offset && offset <= a.end.offset);
    if (parameter < 0) {
        parameter = call.arguments.filter((a) => a.end.offset < offset).length;
    }
    parameter = Math.min(Math.max(parameter, 0), Math.max(symbol.parameters.length - 1, 0));
    return {
        signatures: [
            {
                label: signature(symbol),
                parameters: symbol.parameters.map((p) => ({
                    label: p.name + ': ' + p.type,
                })),
            },
        ],
        activeSignature: 0,
        activeParameter: parameter,
    };
});
connection.onDocumentSymbol(async (params, token): Promise<DocumentSymbol[]> => {
    const current = await snapshot(params.textDocument.uri, token);
    if (!current) {
        return [];
    }
    return current.analysis.semantic.symbols
        .filter((s) => s.kind === 'function' && !s.imported)
        .map((s) => ({
            name: s.name,
            detail: signature(s),
            kind: SymbolKind.Function,
            range: current.range(s.range),
            selectionRange: current.range(s.declaration),
            children: current.analysis.semantic.symbols
                .filter(
                    (child) =>
                        child.kind !== 'function' &&
                        child.scope !== 0 &&
                        child.declaration.start.offset > s.range.start.offset &&
                        child.declaration.end.offset < s.range.end.offset,
                )
                .map((child) => ({
                    name: child.name,
                    detail: child.type,
                    kind: SymbolKind.Variable,
                    range: current.range(child.range),
                    selectionRange: current.range(child.declaration),
                })),
        }));
});
connection.onWorkspaceSymbol(async (params, token) => {
    const indexed = (await workspace?.all(token)) ?? [];
    const external = [];
    for (const document of documents.all()) {
        if (!workspace?.path(document.uri)) {
            const current = await snapshot(document.uri, token);
            if (current) {
                external.push(current);
            }
        }
    }
    const results = [];
    for (const current of [...indexed, ...external]) {
        for (const symbol of current.analysis.semantic.symbols) {
            if (
                symbol.kind === 'function' &&
                !symbol.imported &&
                symbol.name.toLowerCase().includes(params.query.toLowerCase())
            ) {
                results.push({
                    name: symbol.name,
                    kind: SymbolKind.Function,
                    location: {
                        uri: current.document.uri,
                        range: current.range(symbol.declaration),
                    },
                });
            }
        }
    }
    return results;
});
connection.onRequest('mplx/workspaceStatistics', () => workspace?.statistics() ?? []);
connection.languages.semanticTokens.on(async (params, token): Promise<SemanticTokens> => {
    const current = await snapshot(params.textDocument.uri, token);
    if (!current) {
        return {
            data: [],
        };
    }
    const data: number[] = [];
    let lastLine = 0,
        lastCharacter = 0;
    const occurrences = [...current.analysis.semantic.occurrences].sort(
        (a, b) => a.range.start.offset - b.range.start.offset,
    );
    for (const occurrence of occurrences) {
        const symbol = current.analysis.semantic.symbols[occurrence.symbol];
        if (!symbol) {
            continue;
        }
        const range = current.range(occurrence.range),
            line = range.start.line,
            character = range.start.character;
        const length = range.end.character - character;
        if (length <= 0 || range.end.line !== line) {
            continue;
        }
        data.push(
            line - lastLine,
            line === lastLine ? character - lastCharacter : character,
            length,
            symbol.kind === 'function' ? 0 : symbol.kind === 'parameter' ? 1 : 2,
            (occurrence.declaration ? 1 : 0) | (occurrence.write ? 2 : 0),
        );
        lastLine = line;
        lastCharacter = character;
    }
    return {
        data,
    };
});
connection.onShutdown(() => {
    for (const timer of scheduled.values()) clearTimeout(timer);
    scheduled.clear();
    compiler?.dispose();
});
documents.listen(connection);
connection.listen();
