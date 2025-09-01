import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity, Hover, MarkupKind, Location, SignatureHelp, SignatureInformation, ParameterInformation
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync, readFileSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";

// Define types locally to avoid import issues
interface Position {
  line: number;
  character: number;
}

interface Range {
  start: Position;
  end: Position;
}

const connection = createConnection(ProposedFeatures.all);
const documents: any = new (TextDocuments as any)(TextDocument);

let cliPathConfig: string | undefined;
let workspaceRoots: string[] = [];

function wordAt(doc: any, pos: Position): { word: string, range: Range } | null {
  const text = doc.getText();
  const off = doc.offsetAt(pos);
  const re = /[A-Za-z_][A-Za-z0-9_]*/g;
  let m: RegExpExecArray | null;
  while ((m = re.exec(text))){
    const s = m.index, e = s + m[0].length;
    if (off >= s && off <= e){
      const start = doc.positionAt(s);
      const end = doc.positionAt(e);
      return { word: m[0], range: { start, end } };
    }
  }
  return null;
}

connection.onInitialize((params: any) => {
  const init = (params.initializationOptions as any) || {};
  if (typeof init.cliPath === 'string' && init.cliPath.length) {
    cliPathConfig = init.cliPath as string;
  }
  if (Array.isArray(params.workspaceFolders)){
    try {
      workspaceRoots = params.workspaceFolders.map((f: any) => {
        const u: string = String(f.uri || "");
        if (u.startsWith("file://")){
          return decodeURIComponent(u.replace(/^file:\/\//, ""));
        }
        return u;
      });
    } catch { workspaceRoots = []; }
  }
  return { capabilities: { textDocumentSync: TextDocumentSyncKind.Incremental, hoverProvider: true, definitionProvider: true, signatureHelpProvider: { triggerCharacters: ["(", ","] }, referencesProvider: true, renameProvider: { prepareProvider: true } } };
});

documents.onDidChangeContent((change: any) => {
  const uri = change.document.uri;
  const text = change.document.getText();

  // write temp file
  const p = join(tmpdir(), "mplx_" + Math.random().toString(36).slice(2) + ".mplx");
  writeFileSync(p, text, "utf8");

  const binFromEnv = cliPathConfig ?? (process.env.MPLX_CLI && process.env.MPLX_CLI.length ? process.env.MPLX_CLI : undefined);
  const defaultWin = "build\\dev\\Presentation\\tools\\mplx\\mplx.exe";
  const defaultNix = "build/dev/Presentation/tools/mplx/mplx";
  const cliPath = binFromEnv ?? (process.platform === 'win32' ? defaultWin : defaultNix);
  const cli = spawn(cliPath, ["--check", p], { shell: true });
  let out = ""; let err = "";
  cli.stdout.on("data", (d: any) => out += d.toString());
  cli.stderr.on("data", (d: any) => err += d.toString());
  cli.on("close", (_code: any) => {
    let diags: any[] = [];
    const toRangeLegacy = (s: string) => {
      const m = s.match(/^\[line\s+(\d+):(\d+)\]/);
      if (m) {
        const line = Math.max(0, parseInt(m[1], 10) - 1);
        const ch = Math.max(0, parseInt(m[2], 10) - 1);
        return { start: { line, character: ch }, end: { line, character: ch + 1 } };
      }
      return { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } };
    };
    try{
      const j = JSON.parse(out);
      for (const d of (j.diagnostics ?? [])){
        if (typeof d === 'string'){
          const m = String(d);
          diags.push({ message: m, range: toRangeLegacy(m), severity: DiagnosticSeverity.Error, source: "mplx" });
        } else {
          const msg = String(d.message ?? "error");
          const line = Math.max(0, (Number(d.line) || 1) - 1);
          const col  = Math.max(0, (Number(d.col)  || 1) - 1);
          diags.push({ message: msg, range: { start: { line, character: col }, end: { line, character: col+1 } }, severity: DiagnosticSeverity.Error, source: "mplx" });
        }
      }
    } catch {
      if (err.trim().length){
        const m = err.trim();
        diags.push({ message: m, range: toRangeLegacy(m), severity: DiagnosticSeverity.Error, source: "mplx" });
      }
    }
    connection.sendDiagnostics({ uri, diagnostics: diags });
  });
});

connection.onSignatureHelp(async (params: any): Promise<any> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const text = doc.getText();
  // simple parse: find current function name before '('
  const offset = doc.offsetAt(params.position);
  const before = text.slice(0, offset);
  const m = before.match(/([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*$/);
  if (!m) return null;
  const name = m[1];
  // Build a naive signature with up to 5 params as placeholders
  const paramsInfo: any[] = [];
  for (let i=0;i<5;i++){ paramsInfo.push(ParameterInformation.create(`arg${i+1}: i32`)); }
  const sig = SignatureInformation.create(`${name}(...)`, `Call to ${name}`);
  sig.parameters = paramsInfo;
  const help: any = { signatures: [sig], activeSignature: 0, activeParameter: 0 };
  return help;
});

connection.onHover(async (params: any): Promise<any> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const p = join(tmpdir(), "mplx_" + Math.random().toString(36).slice(2) + ".mplx");
  writeFileSync(p, doc.getText(), "utf8");
  const binFromEnv = cliPathConfig ?? (process.env.MPLX_CLI && process.env.MPLX_CLI.length ? process.env.MPLX_CLI : undefined);
  const defaultWin = "build\\dev\\Presentation\\tools\\mplx\\mplx.exe";
  const defaultNix = "build/dev/Presentation/tools/mplx/mplx";
  const cliPath = binFromEnv ?? (process.platform === 'win32' ? defaultWin : defaultNix);
  const cli = spawn(cliPath, ["--symbols", p], { shell: true });
  let out = "";
  cli.stdout.on("data", (d: any) => out += d.toString());
  await new Promise<void>(resolve => cli.on("close", () => resolve()));
  try{
    const j = JSON.parse(out);
    const md = (j.functions ?? []).map((f: any) => `- \`${String(f.name)}(${new Array(Number(f.arity)||0).fill("…").join(", ")})\``).join("\n");
    return { contents: { kind: MarkupKind.Markdown, value: `**Functions**\n${md}` } };
  } catch { return null; }
});

connection.onDefinition(async (params: any) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  // naive go-to-def: find "fn name(...)" matching the word under cursor
  const text = doc.getText();
  const offset = doc.offsetAt(params.position);
  // get word at position
  const reWord = /[A-Za-z_][A-Za-z0-9_]*/g;
  let word: string | null = null;
  {
    const before = text.slice(0, offset);
    const m = before.match(/[A-Za-z_][A-Za-z0-9_]*$/);
    if (m) word = m[0];
  }
  if (!word) return null;
  // search for fn definition
  const re = new RegExp("\\bfn\\s+" + word + "\\b");
  const m = re.exec(text);
  if (!m) return null;
  const idx = m.index + 3; // start of name
  const pos = doc.positionAt(idx);
  const range: any = { start: pos, end: { line: pos.line, character: pos.character + word.length } };
  const loc: any = { uri: params.textDocument.uri, range };
  return [loc];
});

// PrepareRename: return the range of the identifier under cursor if any
connection.onPrepareRename((params: any): any => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const w = wordAt(doc, params.position);
  if (!w) return null;
  return w.range;
});

// Rename: apply across all open documents (workspace-open scope)
connection.onRenameRequest((params: any): any => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const w = wordAt(doc, params.position);
  if (!w) return null;
  const newName: string = String(params.newName || "");
  if (!newName.match(/^[A-Za-z_][A-Za-z0-9_]*$/)) return null;

  const workEdit: any = { changes: {} };
  const allDocs = (documents as any).all ? (documents as any).all() : [doc];
  const rx = new RegExp("\\b" + w.word + "\\b", "g");
  for (const d of allDocs){
    const text = d.getText();
    let m: RegExpExecArray | null;
    const edits: any[] = [];
    while ((m = rx.exec(text))){
      const start = d.positionAt(m.index);
      const end = d.positionAt(m.index + w.word.length);
      edits.push({ range: { start, end }, newText: newName });
    }
    if (edits.length) workEdit.changes[d.uri] = edits;
  }
  return workEdit;
});

// References: scan open documents plus simple FS scan under workspace roots (depth-limited)
connection.onReferences((params: any): any => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const w = wordAt(doc, params.position);
  if (!w) return [];
  const rx = new RegExp("\\b" + w.word + "\\b", "g");
  const out: any[] = [];
  // open docs
  const allDocs = (documents as any).all ? (documents as any).all() : [doc];
  for (const d of allDocs){
    const text = d.getText(); let m: RegExpExecArray | null;
    while ((m = rx.exec(text))){
      const start = d.positionAt(m.index);
      const end = d.positionAt(m.index + w.word.length);
      out.push({ uri: d.uri, range: { start, end } });
    }
  }
  // FS scan (best-effort)
  const files: string[] = [];
  const maxDepth = 3;
  const list = (root: string, depth: number) => {
    if (depth <= 0) return;
    let ents: string[] = [];
    try { ents = readdirSync(root); } catch { return; }
    for (const e of ents){
      if (e === ".git" || e === "node_modules" || e === "build" || e === ".vs" || e === ".vscode") continue;
      const full = join(root, e);
      let st: any; try { st = statSync(full); } catch { continue; }
      if (st.isDirectory()) list(full, depth - 1);
      else if (e.endsWith(".mplx")) files.push(full);
    }
  };
  for (const r of workspaceRoots){ list(r, maxDepth); }
  for (const p of files){
    try{
      const txt = readFileSync(p, "utf8");
      // pseudo-doc
      const lineStarts: number[] = [0];
      for (let i=0;i<txt.length;i++){ if (txt[i]==='\n') lineStarts.push(i+1); }
      const posAt = (offset: number) => {
        if (offset < 0) offset = 0; if (offset > txt.length) offset = txt.length;
        let low = 0, high = lineStarts.length - 1;
        while (low <= high){
          const mid = (low + high) >> 1;
          const start = lineStarts[mid];
          const next = mid+1 < lineStarts.length ? lineStarts[mid+1] : txt.length+1;
          if (offset < start) high = mid - 1; else if (offset >= next) low = mid + 1; else return { line: mid, character: offset - start } as any;
        }
        return { line: 0, character: offset } as any;
      };
      let m: RegExpExecArray | null;
      while ((m = rx.exec(txt))){
        const start = posAt(m.index);
        const end = { line: start.line, character: start.character + w.word.length } as any;
        out.push({ uri: "file://" + p, range: { start, end } });
      }
    } catch {}
  }
  return out;
});

documents.listen(connection);
connection.listen();