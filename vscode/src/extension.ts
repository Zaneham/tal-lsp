import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

const LANGS: Record<string, string> = { tal: 'TAL', ptal: 'pTAL', tacl: 'TACL' };

async function setLang(target: vscode.Uri | undefined, lang: string) {
    const uri = target ?? vscode.window.activeTextEditor?.document.uri;
    if (!uri) return;
    const doc = await vscode.workspace.openTextDocument(uri);
    await vscode.languages.setTextDocumentLanguage(doc, lang);
    if (!vscode.workspace.getConfiguration('tal').get<boolean>('rememberAssociations', true)) return;
    const files = vscode.workspace.getConfiguration('files');
    const assoc = { ...(files.get<Record<string, string>>('associations') ?? {}) };
    const name = path.basename(uri.fsPath);
    for (const k of Object.keys(assoc)) {
        if (k === name || k === `**/${name}`) delete assoc[k];
    }
    assoc[`**/${name}`] = lang;
    const scope = vscode.workspace.workspaceFolders
        ? vscode.ConfigurationTarget.Workspace
        : vscode.ConfigurationTarget.Global;
    await files.update('associations', assoc, scope);
    vscode.window.setStatusBarMessage(`${name} is ${LANGS[lang]} from now on`, 3000);
}

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('tal');
    let serverPath = config.get<string>('serverPath', '');

    if (!serverPath) {
        const ext = process.platform === 'win32' ? '.exe' : '';
        serverPath = context.asAbsolutePath(path.join('bin', `tal-lspd${ext}`));
    }

    const serverOptions: ServerOptions = {
        command: serverPath,
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'tal' },
            { scheme: 'file', language: 'ptal' },
            { scheme: 'file', language: 'tacl' },
            { scheme: 'untitled', language: 'tal' },
            { scheme: 'untitled', language: 'ptal' },
            { scheme: 'untitled', language: 'tacl' }
        ]
    };

    client = new LanguageClient('talLSP', 'TAL Language Server', serverOptions, clientOptions);
    client.start();

    for (const [id, cmd] of [['tal', 'tal.setTal'], ['ptal', 'tal.setPtal'], ['tacl', 'tal.setTacl']]) {
        context.subscriptions.push(
            vscode.commands.registerCommand(cmd, (uri?: vscode.Uri) => setLang(uri, id))
        );
    }

    const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    const refresh = () => {
        const id = vscode.window.activeTextEditor?.document.languageId ?? '';
        if (LANGS[id]) {
            status.text = `$(file-code) ${LANGS[id]}`;
            status.tooltip = 'NonStop language support, right-click a file to change its language';
            status.show();
        } else {
            status.hide();
        }
    };
    context.subscriptions.push(status, vscode.window.onDidChangeActiveTextEditor(refresh));
    refresh();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
