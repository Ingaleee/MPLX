import { TextDocument } from 'vscode-languageserver-textdocument';
import type { Position, Range } from 'vscode-languageserver/node.js';
import { NO_ID, type Analysis, type Occurrence, type Scope, type Span, type Symbol } from './model.js';
const reserved = new Set([
    'fn',
    'let',
    'return',
    'if',
    'else',
    'while',
    'true',
    'false',
    'i32',
    'i64',
    'bool',
    'import',
    'as',
    'export',
]);
export function validateRename(name: string): void {
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name) || reserved.has(name)) {
        throw new Error('Rename requires a nonreserved identifier.');
    }
}
export class Snapshot {
    readonly document: TextDocument;
    private readonly offsets: Uint32Array;
    constructor(
        document: TextDocument,
        readonly analysis: Analysis,
    ) {
        this.document = TextDocument.create(
            document.uri,
            document.languageId,
            document.version,
            document.getText(),
        );
        const text = this.document.getText(),
            bytes = Buffer.byteLength(text, 'utf8');
        this.offsets = new Uint32Array(bytes + 1);
        let byte = 0;
        for (let utf16 = 0; utf16 < text.length;) {
            const point = text.codePointAt(utf16)!;
            const width = point > 0xffff ? 2 : 1;
            const count = Buffer.byteLength(String.fromCodePoint(point), 'utf8');
            for (let b = 0; b < count; ++b) this.offsets[byte + b] = utf16;
            byte += count;
            utf16 += width;
            this.offsets[byte] = utf16;
        }
    }
    byteOffset(position: Position): number {
        return Buffer.byteLength(this.document.getText().slice(0, this.document.offsetAt(position)), 'utf8');
    }
    range(span: Span): Range {
        const start = this.offsets[Math.min(span.start.offset, this.offsets.length - 1)] ?? 0;
        const end = this.offsets[Math.min(span.end.offset, this.offsets.length - 1)] ?? start;
        return {
            start: this.document.positionAt(start),
            end: this.document.positionAt(end),
        };
    }
    occurrence(position: Position): Occurrence | undefined {
        const offset = this.byteOffset(position);
        return this.analysis.semantic.occurrences.find(
            (o) => o.range.start.offset <= offset && offset < o.range.end.offset,
        );
    }
    symbol(position: Position): Symbol | undefined {
        const occurrence = this.occurrence(position);
        return occurrence ? this.analysis.semantic.symbols[occurrence.symbol] : undefined;
    }
    scopeAt(offset: number): Scope | undefined {
        return this.analysis.semantic.scopes
            .filter(
                (s) =>
                    s.id === 0 ||
                    (s.range.start.offset < s.range.end.offset &&
                        s.range.start.offset <= offset &&
                        offset <= s.range.end.offset),
            )
            .sort(
                (a, b) =>
                    b.range.start.offset - a.range.start.offset || a.range.end.offset - b.range.end.offset,
            )[0];
    }
    visible(position: Position): Symbol[] {
        const offset = this.byteOffset(position),
            symbols = this.analysis.semantic.symbols;
        let scope = this.scopeAt(offset);
        const visible = new Map<string, Symbol>();
        while (scope) {
            for (const id of scope.bindings) {
                const s = symbols[id];
                if (s && (s.kind !== 'local' || s.visible_from <= offset) && !visible.has(s.name)) {
                    visible.set(s.name, s);
                }
            }
            scope = scope.parent === NO_ID ? undefined : this.analysis.semantic.scopes[scope.parent];
        }
        return [...visible.values()];
    }
    rename(
        position: Position,
        name: string,
    ): {
        symbol: Symbol;
        occurrences: Occurrence[];
    } {
        validateRename(name);
        const symbol = this.symbol(position);
        if (!symbol) {
            throw new Error('No compiler symbol at this position.');
        }
        if (this.analysis.diagnostics.length) {
            throw new Error('Fix document diagnostics before renaming.');
        }
        const model = this.analysis.semantic;
        const sameScope = model.scopes[symbol.scope];
        if (sameScope?.bindings.some((id) => id !== symbol.id && model.symbols[id]?.name === name)) {
            throw new Error('Rename would duplicate a declaration.');
        }
        const resolve = (offset: number, targetName: string): number => {
            let scope = this.scopeAt(offset);
            while (scope) {
                for (const id of scope.bindings) {
                    const s = model.symbols[id];
                    if (
                        s &&
                        (s.id === symbol.id ? name : s.name) === targetName &&
                        (s.kind !== 'local' || s.visible_from <= offset)
                    ) {
                        return id;
                    }
                }
                scope = scope.parent === NO_ID ? undefined : model.scopes[scope.parent];
            }
            return NO_ID;
        };
        for (const occurrence of model.occurrences) {
            if (occurrence.declaration) {
                continue;
            }
            const bound = model.symbols[occurrence.symbol];
            if (bound && (bound.id === symbol.id || bound.name === name)) {
                if (
                    resolve(occurrence.range.start.offset, bound.id === symbol.id ? name : bound.name) !==
                    bound.id
                ) {
                    throw new Error('Rename would change lexical binding or capture another symbol.');
                }
            }
        }
        return {
            symbol,
            occurrences: model.occurrences.filter((o) => o.symbol === symbol.id),
        };
    }
}
