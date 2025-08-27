import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity
} from "vscode-languageserver/node.js";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync } from "node:fs";
import { join } from "node:path";

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

connection.onInitialize((_params: InitializeParams) => {
  return { capabilities: { textDocumentSync: TextDocumentSyncKind.Incremental } };
});

documents.onDidChangeContent(change => {
  const uri = change.document.uri;
  const text = change.document.getText();

  // write temp file
  const p = join(tmpdir(), "mplx_" + Math.random().toString(36).slice(2) + ".mplx");
  writeFileSync(p, text, "utf8");

  const binFromEnv = process.env.MPLX_CLI && process.env.MPLX_CLI.length ? process.env.MPLX_CLI : undefined;
  const defaultWin = "build\\dev\\src-cpp\\tools\\mplx\\mplx.exe";
  const defaultNix = "build/dev/src-cpp/tools/mplx/mplx";
  const cliPath = binFromEnv ?? (process.platform === 'win32' ? defaultWin : defaultNix);
  const cli = spawn(cliPath, ["--check", p], { shell: true });
  let out = ""; let err = "";
  cli.stdout.on("data", d => out += d.toString());
  cli.stderr.on("data", d => err += d.toString());
  cli.on("close", (_code) => {
    let diags: Diagnostic[] = [];
    const toRange = (s: string) => {
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
      for (const msg of (j.diagnostics ?? [])){
        const m = msg.toString();
        diags.push({
          message: m,
          range: toRange(m),
          severity: DiagnosticSeverity.Error,
          source: "mplx"
        });
      }
    } catch {
      if (err.trim().length){
        const m = err.trim();
        diags.push({
          message: m,
          range: toRange(m),
          severity: DiagnosticSeverity.Error,
          source: "mplx"
        });
      }
    }
    connection.sendDiagnostics({ uri, diagnostics: diags });
  });
});

documents.listen(connection);
connection.listen();