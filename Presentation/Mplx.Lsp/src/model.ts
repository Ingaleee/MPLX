export interface SourcePoint {
    offset: number;
    line: number;
    column: number;
}
export interface Span {
    start: SourcePoint;
    end: SourcePoint;
}
export interface CompilerDiagnostic {
    code: string;
    message: string;
    line: number;
    col: number;
    range: Span;
}
export interface Parameter {
    name: string;
    type: string;
}
export interface Symbol {
    id: number;
    name: string;
    kind: 'function' | 'parameter' | 'local';
    type: string;
    scope: number;
    visible_from: number;
    declaration: Span;
    range: Span;
    parameters: Parameter[];
    definition_path?: string;
    original_name?: string;
    imported?: boolean;
    exported?: boolean;
}
export interface Scope {
    id: number;
    parent: number;
    range: Span;
    bindings: number[];
}
export interface Occurrence {
    symbol: number;
    range: Span;
    declaration: boolean;
    write: boolean;
}
export interface CallSite {
    symbol: number;
    range: Span;
    arguments: Span[];
}
export interface Analysis {
    schema_version: number;
    diagnostics: CompilerDiagnostic[];
    semantic: {
        symbols: Symbol[];
        scopes: Scope[];
        occurrences: Occurrence[];
        calls: CallSite[];
    };
}
export interface WorkspaceDiagnostic extends CompilerDiagnostic {
    path: string;
}
export interface Dependency {
    from: string;
    to: string;
    alias: string;
    range: Span;
    path_range: Span;
    alias_range: Span;
}
export interface WorkspaceFile {
    path: string;
    source: string;
    revision: number;
    analysis: Analysis;
}
export interface BuildStatistics {
    parsed: number;
    syntax_hits: number;
    checked: number;
    semantic_hits: number;
    files: number;
    source_bytes: number;
    linked_functions: number;
    artifact_hit: boolean;
    affected: string[];
}
export interface WorkspaceAnalysis {
    schema_version: number;
    entry: string;
    diagnostics: WorkspaceDiagnostic[];
    files: WorkspaceFile[];
    dependencies: Dependency[];
    statistics: BuildStatistics;
}
export interface SourceOverlay {
    path: string;
    source: string;
}
export const NO_ID = 0xffffffff;
export function signature(symbol: Symbol): string {
    if (symbol.kind === 'function') {
        const parameters = symbol.parameters.map((p) => p.name + ': ' + p.type).join(', ');
        return symbol.name + '(' + parameters + ') -> ' + symbol.type;
    }
    return symbol.kind + ' ' + symbol.name + ': ' + symbol.type;
}
