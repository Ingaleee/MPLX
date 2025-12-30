import { readdir, stat } from 'node:fs/promises';
import { existsSync, realpathSync } from 'node:fs';
import { basename, isAbsolute, relative, resolve, sep } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { TextDocument } from 'vscode-languageserver-textdocument';
import type {
    CancellationToken,
    Location,
    Position,
    TextDocumentEdit,
    TextEdit,
} from 'vscode-languageserver/node.js';
import { CompilerClient } from './compiler-client.js';
import { Snapshot, validateRename } from './snapshot.js';
import type { BuildStatistics, Dependency, Symbol, WorkspaceAnalysis } from './model.js';
interface Project {
    revision: number;
    analysis: WorkspaceAnalysis;
    snapshots: Map<string, Snapshot>;
}
interface Reference {
    snapshot: Snapshot;
    range: Location['range'];
}
const excludedDirectories = new Set([
    '.git',
    '.vs',
    '.codex',
    'build',
    'out',
    'bin',
    'obj',
    'dist',
    'node_modules',
]);
export class WorkspaceIndex {
    private revision = 0;
    private readonly root: string;
    private readonly projects = new Map<string, Project>();
    private readonly dependencies = new Map<string, Dependency[]>();
    private hasDiagnostics = false;
    private readonly snapshots = new Map<string, Snapshot>();
    private readonly jobs = new Map<
        string,
        {
            revision: number;
            promise: Promise<Project | null>;
        }
    >();
    private catalog: string[] | undefined;
    constructor(
        root: string,
        private readonly compiler: CompilerClient,
        private readonly openDocuments: () => TextDocument[],
        private readonly publish: (project: WorkspaceAnalysis, snapshots: Map<string, Snapshot>) => void,
    ) {
        this.root = realpathSync(root);
    }
    path(uri: string): string | undefined {
        try {
            const candidate = fileURLToPath(uri);
            const absolute = existsSync(candidate) ? realpathSync(candidate) : resolve(candidate);
            const path = relative(this.root, absolute);
            if (
                !path ||
                path === '..' ||
                path.startsWith('..' + sep) ||
                isAbsolute(path) ||
                !path.endsWith('.mplx')
            ) {
                return undefined;
            }
            const normalized = path.split(sep).join('/');
            // Match WorkspaceCompiler::path_key: realpath preserves the canonical
            // Unicode spelling, while only ASCII path characters are case-folded.
            return process.platform === 'win32'
                ? normalized.replace(/[A-Z]/g, (character) => character.toLowerCase())
                : normalized;
        } catch {
            return undefined;
        }
    }
    uri(path: string): string {
        const open = this.openDocuments().find((document) => this.path(document.uri) === path);
        if (open) {
            return open.uri;
        }
        const absolute = resolve(this.root, path);
        return pathToFileURL(existsSync(absolute) ? realpathSync(absolute) : absolute).href;
    }
    invalidate(filesChanged = false): void {
        ++this.revision;
        this.projects.clear();
        this.dependencies.clear();
        this.hasDiagnostics = false;
        this.snapshots.clear();
        if (filesChanged) {
            this.catalog = undefined;
        }
    }
    async snapshot(uri: string, token?: CancellationToken): Promise<Snapshot | null> {
        const path = this.path(uri);
        if (!path) {
            return null;
        }
        for (;;) {
            if (token?.isCancellationRequested) {
                return null;
            }
            const generation = this.revision;
            const cached = this.snapshots.get(path);
            if (cached) {
                return cached;
            }
            const project = await this.project(path);
            if (token?.isCancellationRequested) {
                return null;
            }
            if (generation === this.revision) {
                return project?.snapshots.get(path) ?? null;
            }
        }
    }
    definition(current: Snapshot, symbol: Symbol): Location | undefined {
        const path = symbol.definition_path || this.path(current.document.uri);
        const target = path ? this.snapshots.get(path) : current;
        if (!target) {
            return undefined;
        }
        return {
            uri: target.document.uri,
            range: target.range(symbol.declaration),
        };
    }
    dependency(current: Snapshot, position: Position): Dependency | undefined {
        const path = this.path(current.document.uri);
        const offset = current.byteOffset(position);
        return path
            ? this.dependencies
                  .get(path)
                  ?.find(
                      (value) =>
                          value.path_range.start.offset <= offset && offset < value.path_range.end.offset,
                  )
            : undefined;
    }
    async all(token?: CancellationToken): Promise<Snapshot[]> {
        for (;;) {
            const generation = this.revision;
            const paths = new Set(await this.scan());
            for (const document of this.openDocuments()) {
                const path = this.path(document.uri);
                if (path) {
                    paths.add(path);
                }
            }
            if (paths.size > 256) {
                throw new Error('Workspace index exceeds 256 source files.');
            }
            for (const path of [...paths].sort()) {
                if (token?.isCancellationRequested) {
                    return [];
                }
                if (!this.snapshots.has(path)) {
                    await this.project(path);
                }
                if (generation !== this.revision) {
                    break;
                }
            }
            if (generation === this.revision) {
                return [...this.snapshots.values()];
            }
        }
    }
    async references(
        current: Snapshot,
        symbol: Symbol,
        includeDeclaration: boolean,
        token?: CancellationToken,
    ): Promise<Location[]> {
        const identity = this.identity(current, symbol);
        const snapshots = await this.all(token);
        const references = this.collect(snapshots, identity, includeDeclaration);
        return references.map((value) => ({
            uri: value.snapshot.document.uri,
            range: value.range,
        }));
    }
    async rename(
        current: Snapshot,
        position: Position,
        name: string,
        token?: CancellationToken,
    ): Promise<TextDocumentEdit[]> {
        validateRename(name);
        const symbol = current.symbol(position);
        if (!symbol) {
            throw new Error('No compiler symbol at this position.');
        }
        const generation = this.revision;
        const snapshots = await this.all(token);
        if (token?.isCancellationRequested || generation !== this.revision) {
            throw new Error('Workspace changed during rename; retry on the latest snapshot.');
        }
        const identity = this.identity(current, symbol);
        const references = this.collect(snapshots, identity, true);
        if (!references.length) {
            throw new Error('No indexed symbol references.');
        }
        if (this.hasDiagnostics) {
            throw new Error('Fix workspace diagnostics before renaming.');
        }
        const grouped = new Map<
            string,
            {
                snapshot: Snapshot;
                edits: TextEdit[];
            }
        >();
        for (const reference of references) {
            const uri = reference.snapshot.document.uri;
            let group = grouped.get(uri);
            if (!group) {
                group = {
                    snapshot: reference.snapshot,
                    edits: [],
                };
                grouped.set(uri, group);
            }
            group.edits.push({
                range: reference.range,
                newText: name,
            });
        }
        // Compiler validation proves all edits together preserve both acceptance and bindings.
        const overlays = this.overlays();
        const proposed = new Map(overlays.map((value) => [value.path, value.source]));
        for (const [uri, group] of grouped) {
            const path = this.path(uri)!;
            proposed.set(path, TextDocument.applyEdits(group.snapshot.document, group.edits));
        }
        const proposedOverlays = [...proposed].map(([path, source]) => ({
            path,
            source,
        }));
        const targetPath = symbol.definition_path || this.path(current.document.uri)!;
        const validationEntries = new Set<string>([targetPath]);
        for (const uri of grouped.keys()) {
            validationEntries.add(this.path(uri)!);
        }
        for (const project of this.projects.values()) {
            validationEntries.add(project.analysis.entry);
        }
        for (const entry of validationEntries) {
            const candidate = await this.compiler.workspace(this.root, entry, proposedOverlays);
            if (generation !== this.revision || token?.isCancellationRequested) {
                throw new Error('Workspace changed during rename; retry on the latest snapshot.');
            }
            if (candidate.diagnostics.length) {
                throw new Error('Rename would introduce diagnostics or duplicate a declaration.');
            }
            const previous = this.projects.get(entry);
            for (const file of candidate.files) {
                const original = previous?.snapshots.get(file.path) ?? this.snapshots.get(file.path);
                if (!original) {
                    continue;
                }
                const before = original.analysis.semantic.occurrences;
                const after = file.analysis.semantic.occurrences;
                if (
                    before.length !== after.length ||
                    before.some((value, i) => {
                        const next = after[i];
                        if (!next) {
                            return true;
                        }
                        const oldSymbol = original.analysis.semantic.symbols[value.symbol];
                        const newSymbol = file.analysis.semantic.symbols[next.symbol];
                        if (
                            !oldSymbol ||
                            !newSymbol ||
                            value.declaration !== next.declaration ||
                            value.write !== next.write
                        ) {
                            return true;
                        }
                        if (oldSymbol.kind !== 'function') {
                            // Local IDs are deterministic under spelling-only edits.
                            return oldSymbol.id !== newSymbol.id;
                        }
                        const oldIdentity = this.identity(original, oldSymbol);
                        const newIdentity =
                            (newSymbol.definition_path || file.path) +
                            '\0' +
                            (newSymbol.original_name || newSymbol.name);
                        const expected = oldIdentity === identity ? targetPath + '\0' + name : oldIdentity;
                        return newIdentity !== expected;
                    })
                ) {
                    throw new Error('Rename would change lexical binding or capture another symbol.');
                }
            }
        }
        // Ensure closed documents still match the exact contents used for validation.
        for (const [uri, group] of grouped) {
            if (!this.openDocuments().some((document) => document.uri === uri)) {
                const { readFile } = await import('node:fs/promises');
                if ((await readFile(fileURLToPath(uri), 'utf8')) !== group.snapshot.document.getText()) {
                    throw new Error('A file changed on disk during rename; retry.');
                }
            }
        }
        if (generation !== this.revision) {
            throw new Error('Workspace changed during rename; retry.');
        }
        return [...grouped].map(([uri, group]) => ({
            textDocument: {
                uri,
                version: this.openDocuments().find((document) => document.uri === uri)?.version ?? null,
            },
            edits: group.edits,
        }));
    }
    statistics(): BuildStatistics[] {
        return [...this.projects.values()].map((project) => project.analysis.statistics);
    }
    private identity(current: Snapshot, symbol: Symbol): string {
        const path = symbol.definition_path || this.path(current.document.uri) || current.document.uri;
        if (symbol.kind === 'function') {
            return path + '\0' + (symbol.original_name || symbol.name);
        }
        return path + '\0local:' + symbol.id;
    }
    private collect(snapshots: Snapshot[], identity: string, includeDeclaration: boolean): Reference[] {
        const results: Reference[] = [];
        const seen = new Set<string>();
        for (const snapshot of snapshots) {
            for (const occurrence of snapshot.analysis.semantic.occurrences) {
                const symbol = snapshot.analysis.semantic.symbols[occurrence.symbol];
                if (
                    !symbol ||
                    this.identity(snapshot, symbol) !== identity ||
                    (!includeDeclaration && occurrence.declaration)
                ) {
                    continue;
                }
                const key = snapshot.document.uri + ':' + occurrence.range.start.offset;
                if (!seen.has(key)) {
                    seen.add(key);
                    results.push({
                        snapshot,
                        range: snapshot.range(occurrence.range),
                    });
                }
            }
        }
        return results;
    }
    private overlays(): {
        path: string;
        source: string;
    }[] {
        const sources = new Map<string, string>();
        for (const document of this.openDocuments()) {
            const path = this.path(document.uri);
            if (path) {
                sources.set(path, document.getText());
            }
        }
        return [...sources].map(([path, source]) => ({
            path,
            source,
        }));
    }
    private project(path: string): Promise<Project | null> {
        const cached = this.projects.get(path);
        if (cached) {
            return Promise.resolve(cached);
        }
        const old = this.jobs.get(path);
        if (old?.revision === this.revision) {
            return old.promise;
        }
        const generation = this.revision;
        const overlays = this.overlays();
        const versions = new Map(this.openDocuments().map((document) => [document.uri, document.version]));
        const promise = this.compiler
            .workspace(this.root, path, overlays)
            .then((analysis) => {
                if (generation !== this.revision) {
                    return null;
                }
                const snapshots = new Map<string, Snapshot>();
                const uniqueSources = new Map(
                    [...this.snapshots].map(([path, snapshot]) => [
                        path,
                        Buffer.byteLength(snapshot.document.getText(), 'utf8'),
                    ]),
                );
                for (const file of analysis.files) {
                    uniqueSources.set(file.path, Buffer.byteLength(file.source, 'utf8'));
                }
                if (
                    uniqueSources.size > 256 ||
                    [...uniqueSources.values()].reduce((a, b) => a + b, 0) > 16 * 1024 * 1024
                ) {
                    throw new Error('Workspace snapshot count/source budget exceeded.');
                }
                for (const file of analysis.files) {
                    const uri = this.uri(file.path);
                    const existing = this.snapshots.get(file.path);
                    const same =
                        existing &&
                        existing.document.getText() === file.source &&
                        JSON.stringify(existing.analysis) === JSON.stringify(file.analysis);
                    const document = TextDocument.create(uri, 'mplx', versions.get(uri) ?? 0, file.source);
                    const snapshot = same ? existing : new Snapshot(document, file.analysis);
                    file.source = snapshot.document.getText();
                    file.analysis = snapshot.analysis;
                    snapshots.set(file.path, snapshot);
                    this.snapshots.set(file.path, snapshot);
                }
                for (const file of analysis.files) {
                    this.dependencies.set(
                        file.path,
                        analysis.dependencies.filter((edge) => edge.from === file.path),
                    );
                }
                this.hasDiagnostics ||= analysis.diagnostics.length > 0;
                const project = {
                    revision: generation,
                    analysis,
                    snapshots,
                };
                this.projects.set(path, project);
                while (this.projects.size > 8) {
                    this.projects.delete(this.projects.keys().next().value!);
                }
                this.publish(analysis, snapshots);
                return project;
            })
            .finally(() => {
                if (this.jobs.get(path)?.promise === promise) {
                    this.jobs.delete(path);
                }
            });
        this.jobs.set(path, {
            revision: generation,
            promise,
        });
        return promise;
    }
    private async scan(): Promise<string[]> {
        if (this.catalog) {
            return this.catalog;
        }
        const paths: string[] = [];
        const generation = this.revision;
        let visited = 0;
        let totalBytes = 0;
        const walk = async (directory: string, depth: number): Promise<void> => {
            if (depth > 32) {
                throw new Error('Workspace directory depth exceeds 32.');
            }
            const entries = await readdir(directory, {
                withFileTypes: true,
            });
            for (const entry of entries) {
                if (++visited > 16384) {
                    throw new Error('Workspace scan exceeds 16384 directory entries.');
                }
                const path = resolve(directory, entry.name);
                if (entry.isSymbolicLink()) {
                    continue;
                }
                if (entry.isDirectory() && !excludedDirectories.has(entry.name)) {
                    await walk(path, depth + 1);
                } else if (entry.isFile() && entry.name.endsWith('.mplx')) {
                    const size = (await stat(path)).size;
                    totalBytes += size;
                    if (totalBytes > 16 * 1024 * 1024) {
                        throw new Error('Workspace index source budget exceeds 16 MiB.');
                    }
                    if (size > 1024 * 1024) {
                        throw new Error('Workspace source exceeds 1 MiB: ' + basename(path));
                    }
                    const uri = pathToFileURL(path).href;
                    const canonical = this.path(uri);
                    if (canonical) {
                        paths.push(canonical);
                    }
                    if (paths.length > 256) {
                        throw new Error('Workspace index exceeds 256 source files.');
                    }
                }
            }
        };
        await walk(this.root, 0);
        if (generation === this.revision) {
            this.catalog = paths;
        }
        return paths;
    }
}
