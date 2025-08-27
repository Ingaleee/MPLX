import {
  createConnection, ProposedFeatures,
  InitializeParams, InitializeResult,
  TextDocuments
} from 'vscode-languageserver'
import { TextDocument } from 'vscode-languageserver-textdocument'
import { spawn } from 'node:child_process'
import { tmpdir } from 'node:os'
import { writeFileSync } from 'node:fs'
import { join } from 'node:path'

const connection = createConnection(ProposedFeatures.all)
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument)

connection.onInitialize((_params: InitializeParams): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync: 1,
    }
  }
})

documents.onDidChangeContent(change => {
  const uri = change.document.uri
  const text = change.document.getText()

  const p = join(tmpdir(), 'mplx_' + Math.random().toString(36).slice(2) + '.mplx')
  writeFileSync(p, text, 'utf8')

  const binFromEnv = process.env.MPLX_CLI && process.env.MPLX_CLI.length ? process.env.MPLX_CLI : undefined
  const defaultWin = 'build\\dev\\src-cpp\\tools\\mplx\\mplx.exe'
  const defaultNix = 'build/dev/src-cpp/tools/mplx/mplx'
  const cliPath = binFromEnv ?? (process.platform === 'win32' ? defaultWin : defaultNix)

  const cli = spawn(cliPath, ['--check', p], { shell: true })
  let out = ''
  let err = ''
  cli.stdout.on('data', d => (out += d.toString()))
  cli.stderr.on('data', d => (err += d.toString()))
  cli.on('close', (_code) => {
    const diags: any[] = []
    try {
      const j = JSON.parse(out)
      for (const msg of (j.diagnostics ?? [])) {
        diags.push({
          message: String(msg),
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
          severity: 1,
          source: 'mplx'
        })
      }
    } catch {
      if (err.trim().length) {
        diags.push({
          message: err.trim(),
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
          severity: 1,
          source: 'mplx'
        })
      }
    }
    connection.sendDiagnostics({ uri, diagnostics: diags })
  })
})

documents.listen(connection)
connection.listen()