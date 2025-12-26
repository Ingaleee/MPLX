import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import type { Analysis, SourceOverlay, WorkspaceAnalysis } from './model.js';
interface Pending {
    resolve: (analysis: unknown) => void;
    reject: (error: Error) => void;
    timer: ReturnType<typeof setTimeout>;
    bytes: number;
    schema: number;
}
function object(value: unknown): Record<string, unknown> | undefined {
    return value !== null && typeof value === 'object' ? (value as Record<string, unknown>) : undefined;
}
export class CompilerClient {
    private process: ChildProcessWithoutNullStreams | undefined;
    private nextId = 1;
    private buffer = '';
    private readonly pending = new Map<number, Pending>();
    private pendingBytes = 0;
    constructor(
        private readonly executable: string,
        private readonly timeout = 15000,
    ) {}
    analyze(source: string): Promise<Analysis> {
        const data = Buffer.from(source, 'utf8');
        if (data.length > 1024 * 1024) {
            return Promise.reject(new Error('Source exceeds 1 MiB limit.'));
        }
        return this.request<Analysis>(
            (id) => [Buffer.from('ANALYZE ' + id + ' ' + data.length + '\n'), data],
            1,
        );
    }
    workspace(root: string, entry: string, overlays: SourceOverlay[]): Promise<WorkspaceAnalysis> {
        const rootBytes = Buffer.from(root, 'utf8');
        const entryBytes = Buffer.from(entry, 'utf8');
        if (
            !rootBytes.length ||
            !entryBytes.length ||
            rootBytes.length > 4096 ||
            entryBytes.length > 4096 ||
            overlays.length > 256
        ) {
            return Promise.reject(new Error('Invalid workspace frame metadata.'));
        }
        const files: Buffer[] = [];
        let total = 0;
        for (const overlay of overlays) {
            const path = Buffer.from(overlay.path, 'utf8');
            const source = Buffer.from(overlay.source, 'utf8');
            if (!path.length || path.length > 4096 || source.length > 1024 * 1024) {
                return Promise.reject(new Error('Workspace source/path exceeds its limit.'));
            }
            total += source.length;
            if (total > 16 * 1024 * 1024) {
                return Promise.reject(new Error('Workspace sources exceed 16 MiB.'));
            }
            files.push(Buffer.from('FILE ' + path.length + ' ' + source.length + '\n'), path, source);
        }
        return this.request<WorkspaceAnalysis>(
            (id) => [
                Buffer.from(
                    'WORKSPACE ' +
                        id +
                        ' ' +
                        rootBytes.length +
                        ' ' +
                        entryBytes.length +
                        ' ' +
                        overlays.length +
                        '\n',
                ),
                rootBytes,
                entryBytes,
                ...files,
            ],
            2,
        );
    }
    dispose(): void {
        this.fail(new Error('Compiler service stopped.'));
    }
    private request<T>(frame: (id: number) => Buffer[], schema: number): Promise<T> {
        const id = this.nextId++;
        const pieces = frame(id);
        const bytes = pieces.reduce((sum, piece) => sum + piece.length, 0);
        if (this.pending.size >= 64 || this.pendingBytes + bytes > 16 * 1024 * 1024) {
            return Promise.reject(
                new Error('Compiler analysis queue is full. Retry after pending work completes.'),
            );
        }
        if (!this.process) {
            this.start();
        }
        const child = this.process!;
        return new Promise<T>((resolve, reject) => {
            const timer = setTimeout(
                () => this.fail(new Error('Compiler analysis timed out.')),
                this.timeout,
            );
            this.pending.set(id, {
                resolve: (value) => resolve(value as T),
                reject,
                timer,
                bytes,
                schema,
            });
            this.pendingBytes += bytes;
            // One frame write avoids interleaving header/payload segments under backpressure.
            child.stdin.write(Buffer.concat(pieces, bytes));
        });
    }
    private start(): void {
        const child = spawn(this.executable, ['--analysis-server'], {
            stdio: 'pipe',
            shell: false,
            windowsHide: true,
        });
        this.process = child;
        this.buffer = '';
        child.stdout.setEncoding('utf8');
        child.stdout.on('data', (chunk: string) => {
            if (this.process !== child) {
                return;
            }
            this.buffer += chunk;
            if (this.buffer.length > 32 * 1024 * 1024) {
                this.fail(new Error('Compiler response exceeded 32 MiB.'));
                return;
            }
            for (;;) {
                const newline = this.buffer.indexOf('\n');
                if (newline < 0) {
                    break;
                }
                const line = this.buffer.slice(0, newline);
                this.buffer = this.buffer.slice(newline + 1);
                try {
                    const message = object(JSON.parse(line));
                    if (!message || typeof message.id !== 'number') {
                        throw new Error('Invalid compiler response.');
                    }
                    const pending = this.pending.get(message.id);
                    if (!pending) {
                        continue;
                    }
                    const result = object(message.result);
                    if (
                        !result ||
                        result.schema_version !== pending.schema ||
                        !Array.isArray(result.diagnostics)
                    ) {
                        throw new Error('Unsupported compiler protocol.');
                    }
                    if (pending.schema === 1) {
                        const semantic = object(result.semantic);
                        if (!semantic || !Array.isArray(semantic.symbols)) {
                            throw new Error('Malformed semantic model.');
                        }
                    } else if (!Array.isArray(result.files) || !Array.isArray(result.dependencies)) {
                        throw new Error('Malformed workspace model.');
                    }
                    this.pending.delete(message.id);
                    this.pendingBytes -= pending.bytes;
                    clearTimeout(pending.timer);
                    pending.resolve(result);
                } catch {
                    this.fail(new Error('Compiler returned malformed analysis JSON.'));
                    return;
                }
            }
        });
        // stderr cannot corrupt framed protocol responses.
        child.stderr.on('data', () => {});
        child.stdin.on('error', (error) => {
            if (this.process === child) {
                this.fail(error);
            }
        });
        child.on('error', (error) => {
            if (this.process === child) {
                this.fail(error);
            }
        });
        child.on('close', (code) => {
            if (this.process === child) {
                this.fail(new Error('Compiler exited with code ' + code + '.'));
            }
        });
    }
    private fail(error: Error): void {
        const child = this.process;
        this.process = undefined;
        this.buffer = '';
        for (const request of this.pending.values()) {
            clearTimeout(request.timer);
            request.reject(error);
        }
        this.pending.clear();
        this.pendingBytes = 0;
        if (child && !child.killed) {
            child.kill();
        }
    }
}
