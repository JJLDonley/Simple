const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;
let restartInFlight = false;
let restartStatusItem;

function asStringArray(value, fallback) {
  if (!Array.isArray(value)) return fallback;
  return value.filter((v) => typeof v === 'string');
}

function createServerOptions(config) {
  const command = config.get('lspPath', 'simple');
  const args = asStringArray(config.get('lspArgs', ['lsp']), ['lsp']);
  return {
    run: { command, args, transport: TransportKind.stdio },
    debug: { command, args, transport: TransportKind.stdio }
  };
}

function createClientOptions() {
  return {
    documentSelector: [{ language: 'simple' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.simple')
    }
  };
}

function updateRestartStatusVisibility() {
  if (!restartStatusItem) return;
  const editor = vscode.window.activeTextEditor;
  if (editor && editor.document.languageId === 'simple') {
    restartStatusItem.show();
    return;
  }
  restartStatusItem.hide();
}

function createLanguageClient() {
  const config = vscode.workspace.getConfiguration('simple');
  const serverOptions = createServerOptions(config);
  const clientOptions = createClientOptions();
  return new LanguageClient(
    'simpleLanguageServer',
    'Simple Language Server',
    serverOptions,
    clientOptions
  );
}

async function startClient() {
  client = createLanguageClient();
  await client.start();
}

async function restartClient() {
  if (restartInFlight) return;
  restartInFlight = true;
  try {
    if (client) {
      await client.stop();
      client = undefined;
    }
    await startClient();
    vscode.window.setStatusBarMessage('Simple language server restarted', 2000);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    const action = await vscode.window.showErrorMessage(
      `Failed to restart Simple language server: ${message}`,
      'Open Settings'
    );
    if (action === 'Open Settings') {
      vscode.commands.executeCommand('workbench.action.openSettings', '@ext:jjldonley.simple-vscode simple.lsp');
    }
  } finally {
    restartInFlight = false;
  }
}

async function activate(context) {
  restartStatusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  restartStatusItem.command = 'simple.restartLanguageServer';
  restartStatusItem.text = '$(debug-restart) Simple LSP';
  restartStatusItem.tooltip = 'Restart Simple language server';
  context.subscriptions.push(restartStatusItem);

  context.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor(() => {
      updateRestartStatusVisibility();
    })
  );
  updateRestartStatusVisibility();

  context.subscriptions.push(
    vscode.commands.registerCommand('simple.restartLanguageServer', async () => {
      await restartClient();
    })
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      if (!event.affectsConfiguration('simple.lspPath') &&
          !event.affectsConfiguration('simple.lspArgs')) {
        return;
      }
      await restartClient();
    })
  );

  await restartClient();
}

async function deactivate() {
  if (!client) return undefined;
  return client.stop();
}

module.exports = {
  activate,
  deactivate
};
