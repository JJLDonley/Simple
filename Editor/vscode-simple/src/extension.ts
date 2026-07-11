import * as childProcess from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, ServerOptions, TransportKind } from 'vscode-languageclient/node';

const TASK_TYPE = 'simple';
const PROBLEM_MATCHER = '$simple-svm';

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
  configureCompilerPath: 'simple.configureCompilerPath',
  configureOutputDirectory: 'simple.configureOutputDirectory',
  toggleJitDefault: 'simple.toggleJitDefault',
  toggleTrace: 'simple.toggleTrace'
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
    const proc = childProcess.spawn(command, ['version'], { shell: process.platform === 'win32' });
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

function simpleProcessEnv(): NodeJS.ProcessEnv {
  const env = { ...process.env };
  if (vscode.workspace.getConfiguration('simple').get<boolean>('trace', false)) {
    env.SIMPLE_TRACE = '1';
    env.SIMPLE_LSP_TRACE = '1';
  }
  return env;
}

async function createServerOptions(context: vscode.ExtensionContext): Promise<ServerOptions> {
  const command = await requireSvm(context);
  if (!command) throw new Error('Simple compiler not found');
  const config = vscode.workspace.getConfiguration('simple');
  const args = asStringArray(config.get('lspArgs'), ['lsp']);
  const options = { env: simpleProcessEnv() };
  return {
    run: { command, args, transport: TransportKind.stdio, options },
    debug: { command, args, transport: TransportKind.stdio, options }
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

function activeSimpleDocument(showWarning = true): vscode.TextDocument | undefined {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== 'simple') {
    if (showWarning) vscode.window.showWarningMessage('Open a .simple file first.');
    return undefined;
  }
  return editor.document;
}

async function commandDocument(resource?: unknown): Promise<vscode.TextDocument | undefined> {
  if (resource instanceof vscode.Uri && resource.scheme === 'file' && resource.fsPath.endsWith('.simple')) {
    return vscode.workspace.openTextDocument(resource);
  }
  if (typeof resource === 'string') {
    const uri = vscode.Uri.parse(resource);
    if (uri.scheme === 'file' && uri.fsPath.endsWith('.simple')) {
      return vscode.workspace.openTextDocument(uri);
    }
  }
  return activeSimpleDocument();
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
    const proc = childProcess.spawn(svm, args, {
      cwd: workspaceCwd(document),
      env: simpleProcessEnv(),
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

function configuredOutputDirectory(document: vscode.TextDocument): string {
  const config = vscode.workspace.getConfiguration('simple');
  const configured = config.get<string>('outputDirectory', '').trim();
  const file = currentFilePath(document);
  if (!configured) return path.dirname(file);
  if (path.isAbsolute(configured)) return configured;
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  return path.resolve(folder?.uri.fsPath ?? path.dirname(file), configured);
}

function ensureDirectory(dir: string): void {
  fs.mkdirSync(dir, { recursive: true });
}

function outputPath(document: vscode.TextDocument, extension: string): string {
  const file = currentFilePath(document);
  const dir = configuredOutputDirectory(document);
  ensureDirectory(dir);
  return path.join(dir, `${path.basename(file, path.extname(file))}.${extension}`);
}

function binaryOutputPath(document: vscode.TextDocument): string {
  const file = currentFilePath(document);
  const dir = configuredOutputDirectory(document);
  ensureDirectory(dir);
  const suffix = process.platform === 'win32' ? '.exe' : '';
  return path.join(dir, `${path.basename(file, path.extname(file))}${suffix}`);
}

function runArgs(document: vscode.TextDocument, forceJit: boolean): string[] {
  const args = ['run', currentFilePath(document)];
  const jitByDefault = vscode.workspace.getConfiguration('simple').get<boolean>('jitByDefault', false);
  if (forceJit || jitByDefault) args.push('-jit', '--jit-stats');
  return args;
}

function simpleTask(context: vscode.ExtensionContext,
                    name: string,
                    args: string[],
                    scope: vscode.WorkspaceFolder | vscode.TaskScope,
                    cwd?: string): vscode.Task {
  const definition: vscode.TaskDefinition = { type: TASK_TYPE, command: args[0], args: args.slice(1) };
  const execution = new vscode.CustomExecution(async () => {
    const svm = await requireSvm(context);
    if (!svm) throw new Error('Simple compiler not found');
    return new SimpleTaskPseudoterminal(svm, args, cwd);
  });
  const task = new vscode.Task(definition, scope, name, 'Simple', execution, [PROBLEM_MATCHER]);
  task.group = args[0] === 'build' ? vscode.TaskGroup.Build : undefined;
  return task;
}

class SimpleTaskPseudoterminal implements vscode.Pseudoterminal {
  private readonly writeEmitter = new vscode.EventEmitter<string>();
  private readonly closeEmitter = new vscode.EventEmitter<number>();
  private proc: childProcess.ChildProcess | undefined;
  readonly onDidWrite = this.writeEmitter.event;
  readonly onDidClose = this.closeEmitter.event;

  constructor(private readonly command: string,
              private readonly args: string[],
              private readonly cwd?: string) {}

  open(): void {
    this.writeEmitter.fire(`> ${this.command} ${this.args.join(' ')}\r\n`);
    this.proc = childProcess.spawn(this.command, this.args, {
      cwd: this.cwd,
      env: simpleProcessEnv(),
      shell: process.platform === 'win32'
    });
    this.proc.stdout?.on('data', (data: Buffer) => this.writeEmitter.fire(data.toString().replace(/\n/g, '\r\n')));
    this.proc.stderr?.on('data', (data: Buffer) => this.writeEmitter.fire(data.toString().replace(/\n/g, '\r\n')));
    this.proc.on('error', (error: Error) => {
      this.writeEmitter.fire(`${error.message}\r\n`);
      this.closeEmitter.fire(1);
    });
    this.proc.on('close', (code: number | null) => this.closeEmitter.fire(code ?? 1));
  }

  close(): void {
    this.proc?.kill();
  }
}

class SimpleTaskProvider implements vscode.TaskProvider {
  constructor(private readonly context: vscode.ExtensionContext) {}

  async provideTasks(): Promise<vscode.Task[]> {
    const document = activeSimpleDocument(false);
    if (!document) return [];
    const file = currentFilePath(document);
    const cwd = workspaceCwd(document);
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    const scope = folder ?? vscode.TaskScope.Workspace;
    return [
      simpleTask(this.context, 'Simple: check current file', ['check', file], scope, cwd),
      simpleTask(this.context, 'Simple: run current file', runArgs(document, false), scope, cwd),
      simpleTask(this.context, 'Simple: run current file with JIT', runArgs(document, true), scope, cwd),
      simpleTask(this.context, 'Simple: build current file', ['build', file, '--out', binaryOutputPath(document)], scope, cwd),
      simpleTask(this.context, 'Simple: emit SIR', ['emit', '-ir', file, '--out', outputPath(document, 'sir')], scope, cwd),
      simpleTask(this.context, 'Simple: emit SBC', ['emit', '-sbc', file, '--out', outputPath(document, 'sbc')], scope, cwd)
    ];
  }

  async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined> {
    const document = activeSimpleDocument(false);
    if (!document) return undefined;
    const definition = task.definition;
    const command = typeof definition.command === 'string' ? definition.command : undefined;
    const args = Array.isArray(definition.args) ? definition.args.filter((v): v is string => typeof v === 'string') : [];
    if (!command) return undefined;
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    return simpleTask(this.context, task.name, [command, ...args], folder ?? vscode.TaskScope.Workspace, workspaceCwd(document));
  }
}

function registerCommand(context: vscode.ExtensionContext, command: string, callback: (...args: unknown[]) => unknown): void {
  context.subscriptions.push(vscode.commands.registerCommand(command, callback));
}

function registerCommands(context: vscode.ExtensionContext): void {
  registerCommand(context, COMMANDS.restartLanguageServer, () => restartClient(context));
  registerCommand(context, COMMANDS.showLanguageServerOutput, () => lspOutputChannel.show(true));
  registerCommand(context, COMMANDS.configureCompilerPath, () => vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath'));
  registerCommand(context, COMMANDS.configureOutputDirectory, () => vscode.commands.executeCommand('workbench.action.openSettings', 'simple.outputDirectory'));
  registerCommand(context, COMMANDS.toggleJitDefault, async () => {
    const config = vscode.workspace.getConfiguration('simple');
    const current = config.get<boolean>('jitByDefault', false);
    await config.update('jitByDefault', !current, vscode.ConfigurationTarget.Workspace);
    vscode.window.setStatusBarMessage(`Simple JIT default ${!current ? 'enabled' : 'disabled'}`, 2000);
  });
  registerCommand(context, COMMANDS.toggleTrace, async () => {
    const config = vscode.workspace.getConfiguration('simple');
    const current = config.get<boolean>('trace', false);
    await config.update('trace', !current, vscode.ConfigurationTarget.Workspace);
    vscode.window.setStatusBarMessage(`Simple trace ${!current ? 'enabled' : 'disabled'}`, 2000);
    await restartClient(context);
  });
  registerCommand(context, COMMANDS.showVersion, () => runSvm(context, 'Simple version', ['version']));
  registerCommand(context, COMMANDS.showHelp, () => runSvm(context, 'Simple help', ['help']));

  registerCommand(context, COMMANDS.checkCurrentFile, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
    if (doc) await runSvm(context, 'Simple check', ['check', currentFilePath(doc)], doc);
  });
  registerCommand(context, COMMANDS.runCurrentFile, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
    if (doc) await runSvm(context, 'Simple run', runArgs(doc, false), doc);
  });
  registerCommand(context, COMMANDS.runCurrentFileWithJit, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
    if (doc) await runSvm(context, 'Simple run with JIT', runArgs(doc, true), doc);
  });
  registerCommand(context, COMMANDS.buildCurrentFile, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
    if (doc) await runSvm(context, 'Simple build', ['build', currentFilePath(doc), '--out', binaryOutputPath(doc)], doc);
  });
  registerCommand(context, COMMANDS.compileCurrentFile, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
    if (doc) await runSvm(context, 'Simple compile', ['compile', currentFilePath(doc), '--out', binaryOutputPath(doc)], doc);
  });
  registerCommand(context, COMMANDS.emitSir, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
    if (doc) await runSvm(context, 'Simple emit SIR', ['emit', '-ir', currentFilePath(doc), '--out', outputPath(doc, 'sir')], doc);
  });
  registerCommand(context, COMMANDS.emitSbc, async (resource?: unknown) => {
    const doc = await commandDocument(resource);
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
  context.subscriptions.push(vscode.tasks.registerTaskProvider(TASK_TYPE, new SimpleTaskProvider(context)));

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      if (!event.affectsConfiguration('simple.compilerPath') &&
          !event.affectsConfiguration('simple.lspArgs') &&
          !event.affectsConfiguration('simple.trace')) return;
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
