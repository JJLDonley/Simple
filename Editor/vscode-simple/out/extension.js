"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || function (mod) {
    if (mod && mod.__esModule) return mod;
    var result = {};
    if (mod != null) for (var k in mod) if (k !== "default" && Object.prototype.hasOwnProperty.call(mod, k)) __createBinding(result, mod, k);
    __setModuleDefault(result, mod);
    return result;
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.deactivate = exports.activate = void 0;
const childProcess = __importStar(require("child_process"));
const fs = __importStar(require("fs"));
const path = __importStar(require("path"));
const vscode = __importStar(require("vscode"));
const node_1 = require("vscode-languageclient/node");
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
    toggleJitDefault: 'simple.toggleJitDefault'
};
let client;
let restartInFlight = false;
let restartStatusItem;
let lspOutputChannel;
let taskOutputChannel;
function asStringArray(value, fallback) {
    if (!Array.isArray(value))
        return fallback;
    const strings = value.filter((v) => typeof v === 'string');
    return strings.length > 0 ? strings : fallback;
}
function executableName() {
    return process.platform === 'win32' ? 'svm.exe' : 'svm';
}
function pathExists(candidate) {
    try {
        fs.accessSync(candidate, fs.constants.X_OK);
        return true;
    }
    catch {
        return false;
    }
}
async function commandWorks(command) {
    return new Promise((resolve) => {
        const proc = childProcess.spawn(command, ['version'], { shell: process.platform === 'win32' });
        proc.on('error', () => resolve(false));
        proc.on('close', (code) => resolve(code === 0));
    });
}
async function discoverSvm(context) {
    const config = vscode.workspace.getConfiguration('simple');
    const configured = config.get('compilerPath', '').trim();
    if (configured && (path.isAbsolute(configured) ? pathExists(configured) : await commandWorks(configured))) {
        return configured;
    }
    const bundled = path.join(context.extensionPath, 'bin', executableName());
    if (pathExists(bundled))
        return bundled;
    for (const folder of vscode.workspace.workspaceFolders ?? []) {
        const root = folder.uri.fsPath;
        const candidates = [
            path.join(root, 'Compiler', 'bin', executableName()),
            path.join(root, 'Compiler', 'build', 'bin', executableName()),
            path.join(root, 'bin', executableName()),
            path.join(root, 'build', 'bin', executableName())
        ];
        for (const candidate of candidates) {
            if (pathExists(candidate))
                return candidate;
        }
    }
    if (await commandWorks('svm'))
        return 'svm';
    return undefined;
}
async function requireSvm(context) {
    const svm = await discoverSvm(context);
    if (svm)
        return svm;
    const action = await vscode.window.showErrorMessage('Simple compiler not found. Configure simple.compilerPath or add svm to PATH.', 'Open Settings');
    if (action === 'Open Settings') {
        await vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath');
    }
    return undefined;
}
async function createServerOptions(context) {
    const command = await requireSvm(context);
    if (!command)
        throw new Error('Simple compiler not found');
    const config = vscode.workspace.getConfiguration('simple');
    const args = asStringArray(config.get('lspArgs'), ['lsp']);
    return {
        run: { command, args, transport: node_1.TransportKind.stdio },
        debug: { command, args, transport: node_1.TransportKind.stdio }
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
function updateRestartStatusVisibility() {
    if (!restartStatusItem)
        return;
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === 'simple')
        restartStatusItem.show();
    else
        restartStatusItem.hide();
}
async function createLanguageClient(context) {
    const serverOptions = await createServerOptions(context);
    return new node_1.LanguageClient('simpleLanguageServer', 'Simple Language Server', serverOptions, createClientOptions());
}
async function startClient(context) {
    client = await createLanguageClient(context);
    await client.start();
}
async function restartClient(context) {
    if (restartInFlight)
        return;
    restartInFlight = true;
    try {
        if (client) {
            await client.stop();
            client = undefined;
        }
        await startClient(context);
        vscode.window.setStatusBarMessage('Simple language server restarted', 2000);
    }
    catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        const action = await vscode.window.showErrorMessage(`Failed to restart Simple language server: ${message}`, 'Open Settings');
        if (action === 'Open Settings') {
            await vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath');
        }
    }
    finally {
        restartInFlight = false;
    }
}
function activeSimpleDocument(showWarning = true) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'simple') {
        if (showWarning)
            vscode.window.showWarningMessage('Open a .simple file first.');
        return undefined;
    }
    return editor.document;
}
async function commandDocument(resource) {
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
function workspaceCwd(document) {
    if (document) {
        const folder = vscode.workspace.getWorkspaceFolder(document.uri);
        if (folder)
            return folder.uri.fsPath;
        if (document.uri.scheme === 'file')
            return path.dirname(document.uri.fsPath);
    }
    return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}
async function runSvm(context, label, args, document) {
    const svm = await requireSvm(context);
    if (!svm)
        return;
    taskOutputChannel.show(true);
    taskOutputChannel.appendLine(`> ${svm} ${args.join(' ')}`);
    await new Promise((resolve) => {
        const proc = childProcess.spawn(svm, args, {
            cwd: workspaceCwd(document),
            shell: process.platform === 'win32'
        });
        proc.stdout.on('data', (data) => taskOutputChannel.append(data.toString()));
        proc.stderr.on('data', (data) => taskOutputChannel.append(data.toString()));
        proc.on('error', (error) => {
            taskOutputChannel.appendLine(error.message);
            vscode.window.showErrorMessage(`${label} failed: ${error.message}`);
            resolve();
        });
        proc.on('close', (code) => {
            const ok = code === 0;
            taskOutputChannel.appendLine(`\n${label} ${ok ? 'completed' : `failed (${code ?? 'signal'})`}`);
            if (!ok)
                vscode.window.showErrorMessage(`${label} failed. See Simple output.`);
            resolve();
        });
    });
}
function currentFilePath(document) {
    if (document.uri.scheme !== 'file')
        throw new Error('Current document is not a file.');
    return document.uri.fsPath;
}
function configuredOutputDirectory(document) {
    const config = vscode.workspace.getConfiguration('simple');
    const configured = config.get('outputDirectory', '').trim();
    const file = currentFilePath(document);
    if (!configured)
        return path.dirname(file);
    if (path.isAbsolute(configured))
        return configured;
    const folder = vscode.workspace.getWorkspaceFolder(document.uri);
    return path.resolve(folder?.uri.fsPath ?? path.dirname(file), configured);
}
function ensureDirectory(dir) {
    fs.mkdirSync(dir, { recursive: true });
}
function outputPath(document, extension) {
    const file = currentFilePath(document);
    const dir = configuredOutputDirectory(document);
    ensureDirectory(dir);
    return path.join(dir, `${path.basename(file, path.extname(file))}.${extension}`);
}
function binaryOutputPath(document) {
    const file = currentFilePath(document);
    const dir = configuredOutputDirectory(document);
    ensureDirectory(dir);
    const suffix = process.platform === 'win32' ? '.exe' : '';
    return path.join(dir, `${path.basename(file, path.extname(file))}${suffix}`);
}
function runArgs(document, forceJit) {
    const args = ['run', currentFilePath(document)];
    const jitByDefault = vscode.workspace.getConfiguration('simple').get('jitByDefault', false);
    if (forceJit || jitByDefault)
        args.push('-jit', '--jit-stats');
    return args;
}
function simpleTask(context, name, args, scope, cwd) {
    const definition = { type: TASK_TYPE, command: args[0], args: args.slice(1) };
    const execution = new vscode.CustomExecution(async () => {
        const svm = await requireSvm(context);
        if (!svm)
            throw new Error('Simple compiler not found');
        return new SimpleTaskPseudoterminal(svm, args, cwd);
    });
    const task = new vscode.Task(definition, scope, name, 'Simple', execution, [PROBLEM_MATCHER]);
    task.group = args[0] === 'build' ? vscode.TaskGroup.Build : undefined;
    return task;
}
class SimpleTaskPseudoterminal {
    constructor(command, args, cwd) {
        this.command = command;
        this.args = args;
        this.cwd = cwd;
        this.writeEmitter = new vscode.EventEmitter();
        this.closeEmitter = new vscode.EventEmitter();
        this.onDidWrite = this.writeEmitter.event;
        this.onDidClose = this.closeEmitter.event;
    }
    open() {
        this.writeEmitter.fire(`> ${this.command} ${this.args.join(' ')}\r\n`);
        this.proc = childProcess.spawn(this.command, this.args, {
            cwd: this.cwd,
            shell: process.platform === 'win32'
        });
        this.proc.stdout?.on('data', (data) => this.writeEmitter.fire(data.toString().replace(/\n/g, '\r\n')));
        this.proc.stderr?.on('data', (data) => this.writeEmitter.fire(data.toString().replace(/\n/g, '\r\n')));
        this.proc.on('error', (error) => {
            this.writeEmitter.fire(`${error.message}\r\n`);
            this.closeEmitter.fire(1);
        });
        this.proc.on('close', (code) => this.closeEmitter.fire(code ?? 1));
    }
    close() {
        this.proc?.kill();
    }
}
class SimpleTaskProvider {
    constructor(context) {
        this.context = context;
    }
    async provideTasks() {
        const document = activeSimpleDocument(false);
        if (!document)
            return [];
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
    async resolveTask(task) {
        const document = activeSimpleDocument(false);
        if (!document)
            return undefined;
        const definition = task.definition;
        const command = typeof definition.command === 'string' ? definition.command : undefined;
        const args = Array.isArray(definition.args) ? definition.args.filter((v) => typeof v === 'string') : [];
        if (!command)
            return undefined;
        const folder = vscode.workspace.getWorkspaceFolder(document.uri);
        return simpleTask(this.context, task.name, [command, ...args], folder ?? vscode.TaskScope.Workspace, workspaceCwd(document));
    }
}
function registerCommand(context, command, callback) {
    context.subscriptions.push(vscode.commands.registerCommand(command, callback));
}
function registerCommands(context) {
    registerCommand(context, COMMANDS.restartLanguageServer, () => restartClient(context));
    registerCommand(context, COMMANDS.showLanguageServerOutput, () => lspOutputChannel.show(true));
    registerCommand(context, COMMANDS.configureCompilerPath, () => vscode.commands.executeCommand('workbench.action.openSettings', 'simple.compilerPath'));
    registerCommand(context, COMMANDS.configureOutputDirectory, () => vscode.commands.executeCommand('workbench.action.openSettings', 'simple.outputDirectory'));
    registerCommand(context, COMMANDS.toggleJitDefault, async () => {
        const config = vscode.workspace.getConfiguration('simple');
        const current = config.get('jitByDefault', false);
        await config.update('jitByDefault', !current, vscode.ConfigurationTarget.Workspace);
        vscode.window.setStatusBarMessage(`Simple JIT default ${!current ? 'enabled' : 'disabled'}`, 2000);
    });
    registerCommand(context, COMMANDS.showVersion, () => runSvm(context, 'Simple version', ['version']));
    registerCommand(context, COMMANDS.showHelp, () => runSvm(context, 'Simple help', ['help']));
    registerCommand(context, COMMANDS.checkCurrentFile, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple check', ['check', currentFilePath(doc)], doc);
    });
    registerCommand(context, COMMANDS.runCurrentFile, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple run', runArgs(doc, false), doc);
    });
    registerCommand(context, COMMANDS.runCurrentFileWithJit, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple run with JIT', runArgs(doc, true), doc);
    });
    registerCommand(context, COMMANDS.buildCurrentFile, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple build', ['build', currentFilePath(doc), '--out', binaryOutputPath(doc)], doc);
    });
    registerCommand(context, COMMANDS.compileCurrentFile, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple compile', ['compile', currentFilePath(doc), '--out', binaryOutputPath(doc)], doc);
    });
    registerCommand(context, COMMANDS.emitSir, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple emit SIR', ['emit', '-ir', currentFilePath(doc), '--out', outputPath(doc, 'sir')], doc);
    });
    registerCommand(context, COMMANDS.emitSbc, async (resource) => {
        const doc = await commandDocument(resource);
        if (doc)
            await runSvm(context, 'Simple emit SBC', ['emit', '-sbc', currentFilePath(doc), '--out', outputPath(doc, 'sbc')], doc);
    });
}
async function activate(context) {
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
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(async (event) => {
        if (!event.affectsConfiguration('simple.compilerPath') && !event.affectsConfiguration('simple.lspArgs'))
            return;
        await restartClient(context);
    }));
    await restartClient(context);
}
exports.activate = activate;
async function deactivate() {
    if (!client)
        return;
    await client.stop();
    client = undefined;
}
exports.deactivate = deactivate;
