import { createConnection, ProposedFeatures, InitializeParams, InitializeResult, TextDocuments, TextDocument } from 'vscode-languageserver'

const connection = createConnection(ProposedFeatures.all)
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument)

connection.onInitialize((_params: InitializeParams): InitializeResult => {
  return {
    capabilities: {
      textDocumentSync: 1,
    }
  }
})

documents.onDidOpen(e => {
  connection.console.log(`Opened ${e.document.uri}`)
})

documents.listen(connection)
connection.listen()