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

  const cli = spawn("build/dev/src-cpp/tools/mplx/mplx", ["--check", p], { shell: true });
  let out = ""; let err = "";
  cli.stdout.on("data", d => out += d.toString());
  cli.stderr.on("data", d => err += d.toString());
  cli.on("close", (_code) => {
    let diags: Diagnostic[] = [];
    try{
      const j = JSON.parse(out);
      for (const msg of (j.diagnostics ?? [])){
        // Сѓ РЅР°СЃ СЃРµР№С‡Р°СЃ С‚РѕР»СЊРєРѕ С‚РµРєСЃС‚, Р±РµР· С‚РѕС‡РЅС‹С… РїРѕР·РёС†РёР№ вЂ” РєР»Р°РґС‘Рј РІ (0,0)
        diags.push({
          message: msg.toString(),
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
          severity: DiagnosticSeverity.Error,
          source: "mplx"
        });
      }
    } catch {
      if (err.trim().length){
        diags.push({
          message: err.trim(),
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
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