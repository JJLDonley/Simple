const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

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
    documentSelector: [{ scheme: 'file', language: 'simple' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.simple')
    }
  };
}

function activate(context) {
  const config = vscode.workspace.getConfiguration('simple');
  const serverOptions = createServerOptions(config);
  const clientOptions = createClientOptions();

  client = new LanguageClient(
    'simpleLanguageServer',
    'Simple Language Server',
    serverOptions,
    clientOptions
  );

  context.subscriptions.push(client.start());
}

async function deactivate() {
  if (!client) return undefined;
  return client.stop();
}

module.exports = {
  activate,
  deactivate
};
