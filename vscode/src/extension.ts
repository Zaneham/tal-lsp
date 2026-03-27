import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('tal');
    let serverPath = config.get<string>('serverPath', '');

    // Use bundled binary if not specified
    if (!serverPath) {
        const ext = process.platform === 'win32' ? '.exe' : '';
        serverPath = context.asAbsolutePath(
            path.join('bin', `tal-lspd${ext}`)
        );
    }

    const serverOptions: ServerOptions = {
        command: serverPath,
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'tal' }
        ],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{tal,TAL}')
        }
    };

    client = new LanguageClient(
        'talLSP',
        'TAL Language Server',
        serverOptions,
        clientOptions
    );

    client.start();

    const statusBarItem = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Right,
        100
    );
    statusBarItem.text = '$(file-code) TAL';
    statusBarItem.tooltip = 'TAL Language Support (Tandem/HP NonStop)';
    statusBarItem.show();
    context.subscriptions.push(statusBarItem);
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
