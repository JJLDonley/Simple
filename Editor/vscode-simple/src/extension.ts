import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, ServerOptions, TransportKind } from 'vscode-languageclient/node';

const COMMANDS = {
  restartLanguageServer: 'simple.restartLanguageServer',
  checkCurrentFile: 'simple.checkCurrentFile',
  runCurrentFile: 'simple.runCurrentFile',
  runCurrentFileWithJit: 'simple.runCurrentFileWithJit',
  buildCurrentFile: 'simple.buildCurrentFile',
  compileCurrentFile: 'simple.compileCurrentFile',
  emitSir: 'simple.emitSir',
  emitSbc: 'simple.emitSbc',
  showLanguageServerOutput: 'simple.showLanguageServerOutput',
  showVersion: 'simple.showVersion',
  showHelp: 'simple.showHelp',
  configureCompilerPath: 'simple.configureCompilerPath'
} as const;

let client: LanguageClient | undefined;
let restartInFlight = false;
let restartStatusItem: vscode.StatusBarItem | undefined;
let lspOutputChannel: vscode.OutputChannel;
let taskOutputChannel: vscode.OutputChannel;

function asStringArray(value: unknown, fallback: string[]): string[] {
  if (!Array.isArray(value)) return fallback;
  const strings = value.filter((v): v is string => typeof v === 'string');
  return strings.length > 0 ? strings : fallback;
}

function executableName(): string {
  return process.platform === 'win32' ? 'svm.exe' : 'svm';
}

function pathExists(candidate: string): boolean {
  try {
    fs.accessSync(candidate, fs.constants.X_OK);
    return true;
  } catch {
    return false;
  }
}

async function commandWorks(command: string): Promise<boolean> {
  return new Promise((resolve) => {
    const proc = require('child_process').spawn(command, ['version'], { shell: process.platform === 'win32' });
    proc.on('error', () => resolve(false));
    proc.on('close', (code: number | null) => resolve(code === 0));
  });
}

async function discoverSvm(context: vscode.ExtensionContext): Promise<string | undefined> {
  const config = vscode.workspace.getConfiguration('simple');
  const configured = config.get<string>('compilerPath', '').trim();
  if (configured && (path.isAbsolute(configured) ? pathExists(configured) : await commandWorks(configured))) {
    return configured;
  }

  const bundled = path.join(context.extensionPath, 'bin', executableName());
  if (pathExists(bundled)) return bundled;

  for (const folder of vscode.workspace.workspaceFolders ?? []) {
    const root = folder.uri.fsPath;
    const candidates = [
      path.join(root, 'Compiler', 'bin', executableName()),
      path.join(root, 'Compiler', 'build', 'bin', executableName()),
      path.join(root, 'bin', executableName()),
      path.join(root, 'build', 'bin', executableName())
    ];
    for (const candidate of candidates) {
      if (pathExists(candidate)) return candidate;
    }
  }

  if (await commandWorks('svm')) return 'svm';
  return undefined;
}

async function requireSvm(context: vscode.ExtensionContext): Promise<string | undefined> {
  const svm = await discoverSvm(context);
  if (svm) return svm;
  const action = await vscode.window.showErrorMessage(
    'Simple compiler not found. Configure simple.compilerPath or add svm to PATH.',
    'Open Settings'
  );
  if (action === 'Open Settings') {
    await vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath');
  }
  return undefined;
}

async function createServerOptions(context: vscode.ExtensionContext): Promise<ServerOptions> {
  const command = await requireSvm(context);
  if (!command) throw new Error('Simple compiler not found');
  const config = vscode.workspace.getConfiguration('simple');
  const args = asStringArray(config.get('lspArgs'), ['lsp']);
  return {
    run: { command, args, transport: TransportKind.stdio },
    debug: { command, args, transport: TransportKind.stdio }
  };
}

function createClientOptions() {
  return {
    documentSelector: [{ language: 'simple' }],
    outputChannel: lspOutputChannel,
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.simple')
    }
  };
}

function updateRestartStatusVisibility(): void {
  if (!restartStatusItem) return;
  const editor = vscode.window.activeTextEditor;
  if (editor && editor.document.languageId === 'simple') restartStatusItem.show();
  else restartStatusItem.hide();
}

async function createLanguageClient(context: vscode.ExtensionContext): Promise<LanguageClient> {
  const serverOptions = await createServerOptions(context);
  return new LanguageClient(
    'simpleLanguageServer',
    'Simple Language Server',
    serverOptions,
    createClientOptions()
  );
}

async function startClient(context: vscode.ExtensionContext): Promise<void> {
  client = await createLanguageClient(context);
  await client.start();
}

async function restartClient(context: vscode.ExtensionContext): Promise<void> {
  if (restartInFlight) return;
  restartInFlight = true;
  try {
    if (client) {
      await client.stop();
      client = undefined;
    }
    await startClient(context);
    vscode.window.setStatusBarMessage('Simple language server restarted', 2000);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    const action = await vscode.window.showErrorMessage(
      `Failed to restart Simple language server: ${message}`,
      'Open Settings'
    );
    if (action === 'Open Settings') {
      await vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath');
    }
  } finally {
    restartInFlight = false;
  }
}

function activeSimpleDocument(): vscode.TextDocument | undefined {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== 'simple') {
    vscode.window.showWarningMessage('Open a .simple file first.');
    return undefined;
  }
  return editor.document;
}

function workspaceCwd(document?: vscode.TextDocument): string | undefined {
  if (document) {
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    if (folder) return folder.uri.fsPath;
    if (document.uri.scheme === 'file') return path.dirname(document.uri.fsPath);
  }
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

async function runSvm(context: vscode.ExtensionContext, label: string, args: string[], document?: vscode.TextDocument): Promise<void> {
  const svm = await requireSvm(context);
  if (!svm) return;
  taskOutputChannel.show(true);
  taskOutputChannel.appendLine(`> ${svm} ${args.join(' ')}`);
  await new Promise<void>((resolve) => {
    const proc = require('child_process').spawn(svm, args, {
      cwd: workspaceCwd(document),
      shell: process.platform === 'win32'
    });
    proc.stdout.on('data', (data: Buffer) => taskOutputChannel.append(data.toString()));
    proc.stderr.on('data', (data: Buffer) => taskOutputChannel.append(data.toString()));
    proc.on('error', (error: Error) => {
      taskOutputChannel.appendLine(error.message);
      vscode.window.showErrorMessage(`${label} failed: ${error.message}`);
      resolve();
    });
    proc.on('close', (code: number | null) => {
      const ok = code === 0;
      taskOutputChannel.appendLine(`\n${label} ${ok ? 'completed' : `failed (${code ?? 'signal'})`}`);
      if (!ok) vscode.window.showErrorMessage(`${label} failed. See Simple output.`);
      resolve();
    });
  });
}

function currentFilePath(document: vscode.TextDocument): string {
  if (document.uri.scheme !== 'file') throw new Error('Current document is not a file.');
  return document.uri.fsPath;
}

function outputPath(document: vscode.TextDocument, extension: string): string {
  const file = currentFilePath(document);
  return path.join(path.dirname(file), `${path.basename(file, path.extname(file))}.${extension}`);
}

function registerCommand(context: vscode.ExtensionContext, command: string, callback: () => unknown): void {
  context.subscriptions.push(vscode.commands.registerCommand(command, callback));
}

function registerCommands(context: vscode.ExtensionContext): void {
  registerCommand(context, COMMANDS.restartLanguageServer, () => restartClient(context));
  registerCommand(context, COMMANDS.showLanguageServerOutput, () => lspOutputChannel.show(true));
  registerCommand(context, COMMANDS.configureCompilerPath, () => vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath'));
  registerCommand(context, COMMANDS.showVersion, () => runSvm(context, 'Simple version', ['version']));
  registerCommand(context, COMMANDS.showHelp, () => runSvm(context, 'Simple help', ['help']));

  registerCommand(context, COMMANDS.checkCurrentFile, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple check', ['check', currentFilePath(doc)], doc);
  });
  registerCommand(context, COMMANDS.runCurrentFile, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple run', ['run', currentFilePath(doc)], doc);
  });
  registerCommand(context, COMMANDS.runCurrentFileWithJit, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple run with JIT', ['run', currentFilePath(doc), '-jit', '--jit-stats'], doc);
  });
  registerCommand(context, COMMANDS.buildCurrentFile, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple build', ['build', currentFilePath(doc)], doc);
  });
  registerCommand(context, COMMANDS.compileCurrentFile, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple compile', ['compile', currentFilePath(doc)], doc);
  });
  registerCommand(context, COMMANDS.emitSir, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple emit SIR', ['emit', '-ir', currentFilePath(doc), '--out', outputPath(doc, 'sir')], doc);
  });
  registerCommand(context, COMMANDS.emitSbc, async () => {
    const doc = activeSimpleDocument();
    if (doc) await runSvm(context, 'Simple emit SBC', ['emit', '-sbc', currentFilePath(doc), '--out', outputPath(doc, 'sbc')], doc);
  });
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  lspOutputChannel = vscode.window.createOutputChannel('Simple Language Server');
  taskOutputChannel = vscode.window.createOutputChannel('Simple');
  context.subscriptions.push(lspOutputChannel, taskOutputChannel);

  restartStatusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  restartStatusItem.command = COMMANDS.restartLanguageServer;
  restartStatusItem.text = '$(debug-restart) Simple LSP';
  restartStatusItem.tooltip = 'Restart Simple language server';
  context.subscriptions.push(restartStatusItem);

  context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor(updateRestartStatusVisibility));
  updateRestartStatusVisibility();
  registerCommands(context);

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      if (!event.affectsConfiguration('simple.compilerPath') && !event.affectsConfiguration('simple.lspArgs')) return;
      await restartClient(context);
    })
  );

  await restartClient(context);
}

export async function deactivate(): Promise<void> {
  if (!client) return;
  await client.stop();
  client = undefined;
}
