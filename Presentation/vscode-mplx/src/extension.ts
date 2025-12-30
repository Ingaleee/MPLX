import * as vscode from 'vscode';
import {
    LanguageClient,
    TransportKind,
    type LanguageClientOptions,
    type ServerOptions,
} from 'vscode-languageclient/node';
let client: LanguageClient | undefined;
export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const output = vscode.window.createOutputChannel('MPLX');
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.mplx');
    context.subscriptions.push(watcher);
    const server: ServerOptions = {
        module: context.asAbsolutePath('server/server.js'),
        transport: TransportKind.ipc,
    };
    const options: LanguageClientOptions = {
        documentSelector: [
            {
                scheme: 'file',
                language: 'mplx',
            },
            {
                scheme: 'untitled',
                language: 'mplx',
            },
        ],
        synchronize: {
            configurationSection: 'mplx',
            fileEvents: watcher,
        },
        outputChannel: output,
        initializationOptions: {
            cliPath: vscode.workspace.getConfiguration('mplx').get<string>('cliPath') ?? '',
        },
    };
    client = new LanguageClient('mplx', 'MPLX Language Server', server, options);
    context.subscriptions.push(
        output,
        vscode.commands.registerCommand('mplx.restartServer', async () => {
            if (client) {
                await client.stop();
                await client.start();
            }
        }),
    );
    await client.start();
}
export function deactivate(): Promise<void> | undefined {
    return client?.stop();
}
