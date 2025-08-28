import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity, Hover, MarkupKind, Location, Position, Range, SignatureHelp, SignatureInformation, ParameterInformation
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync } from "node:fs";
import { join } from "node:path";

const connection = createConnection(ProposedFeatures.all);
const documents: any = new (TextDocuments as any)(TextDocument);

let cliPathConfig: string | undefined;

connection.onInitialize((params: any) => {
  const init = (params.initializationOptions as any) || {};
  if (typeof init.cliPath === 'string' && init.cliPath.length) {
    cliPathConfig = init.cliPath as string;
  }
  return { capabilities: { textDocumentSync: TextDocumentSyncKind.Incremental, hoverProvider: true, definitionProvider: true, signatureHelpProvider: { triggerCharacters: ["(", ","] } } };
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

documents.listen(connection);
connection.listen();