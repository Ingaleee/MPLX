import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity, Hover, MarkupKind
} from "vscode-languageserver/node.js";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync } from "node:fs";
import { join } from "node:path";

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

let cliPathConfig: string | undefined;

connection.onInitialize((params: InitializeParams) => {
  const init = (params.initializationOptions as any) || {};
  if (typeof init.cliPath === 'string' && init.cliPath.length) {
    cliPathConfig = init.cliPath as string;
  }
  return { capabilities: { textDocumentSync: TextDocumentSyncKind.Incremental, hoverProvider: true } };
});

documents.onDidChangeContent(change => {
  const uri = change.document.uri;
  const text = change.document.getText();

  // write temp file
  const p = join(tmpdir(), "mplx_" + Math.random().toString(36).slice(2) + ".mplx");
  writeFileSync(p, text, "utf8");

  const binFromEnv = cliPathConfig ?? (process.env.MPLX_CLI && process.env.MPLX_CLI.length ? process.env.MPLX_CLI : undefined);
  const defaultWin = "build\\dev\\src-cpp\\tools\\mplx\\mplx.exe";
  const defaultNix = "build/dev/src-cpp/tools/mplx/mplx";
  const cliPath = binFromEnv ?? (process.platform === 'win32' ? defaultWin : defaultNix);
  const cli = spawn(cliPath, ["--check", p], { shell: true });
  let out = ""; let err = "";
  cli.stdout.on("data", d => out += d.toString());
  cli.stderr.on("data", d => err += d.toString());
  cli.on("close", (_code) => {
    let diags: Diagnostic[] = [];
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

connection.onHover(async (params): Promise<Hover | null> => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  // naive symbol extraction: list top-level fn names
  const text = doc.getText();
  const fnMatches = Array.from(text.matchAll(/\bfn\s+([A-Za-z_][A-Za-z0-9_]*)/g)).map(m => m[1]);
  if (!fnMatches.length) return null;
  const md = fnMatches.map(n => `- \`${n}(…)\``).join("\n");
  return { contents: { kind: MarkupKind.Markdown, value: `**Functions**\n${md}` } };
});

documents.listen(connection);
connection.listen();