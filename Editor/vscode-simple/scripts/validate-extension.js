#!/usr/bin/env node
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
const compilerVersion = fs.readFileSync(path.resolve(root, '..', '..', 'VERSION'), 'utf8').trim();
const expectedVsixVersion = compilerVersion.startsWith('v') ? compilerVersion.slice(1) : compilerVersion;
const fail = (message) => {
  console.error(message);
  process.exit(1);
};

if (pkg.version !== expectedVsixVersion) fail(`VSIX version ${pkg.version} does not match VERSION ${compilerVersion}`);
if (pkg.main !== './out/extension.js') fail('package main must point at compiled TypeScript output');
if (!fs.existsSync(path.join(root, pkg.main))) fail(`compiled extension missing: ${pkg.main}`);
if (pkg.contributes?.configuration?.properties?.['simple.lspPath']) fail('simple.lspPath is obsolete; use simple.compilerPath');
if (!pkg.contributes?.configuration?.properties?.['simple.compilerPath']) fail('simple.compilerPath setting missing');
if (!pkg.contributes?.configuration?.properties?.['simple.outputDirectory']) fail('simple.outputDirectory setting missing');
if (!pkg.contributes?.configuration?.properties?.['simple.jitByDefault']) fail('simple.jitByDefault setting missing');
if (!pkg.contributes?.configuration?.properties?.['simple.trace']) fail('simple.trace setting missing');
if (!Array.isArray(pkg.contributes?.taskDefinitions) || !pkg.contributes.taskDefinitions.some((t) => t.type === 'simple')) fail('simple task definition missing');
if (!Array.isArray(pkg.contributes?.problemMatchers) || !pkg.contributes.problemMatchers.some((m) => m.name === 'simple-svm')) fail('simple problem matcher missing');
if (!Array.isArray(pkg.contributes?.menus?.['explorer/context']) || pkg.contributes.menus['explorer/context'].length === 0) fail('explorer context entries missing');
const simpleLanguage = (pkg.contributes?.languages ?? []).find((l) => l.id === 'simple');
for (const ext of ['.simple', '.sir', '.sbc']) {
  if (!simpleLanguage?.extensions?.includes(ext)) fail(`language extension missing: ${ext}`);
}
const explorerText = JSON.stringify(pkg.contributes.menus['explorer/context']);
for (const ext of ['.simple', '.sir', '.sbc']) {
  if (!explorerText.includes(ext)) fail(`explorer context missing: ${ext}`);
}

const commands = new Set((pkg.contributes?.commands ?? []).map((c) => c.command));
for (const command of [
  'simple.checkCurrentFile',
  'simple.runCurrentFile',
  'simple.runCurrentFileWithJit',
  'simple.buildCurrentFile',
  'simple.compileCurrentFile',
  'simple.emitSir',
  'simple.emitSbc',
  'simple.restartLanguageServer',
  'simple.showLanguageServerOutput',
  'simple.showVersion',
  'simple.showHelp',
  'simple.configureCompilerPath',
  'simple.configureOutputDirectory',
  'simple.toggleJitDefault',
  'simple.toggleTrace'
]) {
  if (!commands.has(command)) fail(`command missing: ${command}`);
}

const grammar = JSON.parse(fs.readFileSync(path.join(root, 'syntaxes', 'simple.tmLanguage.json'), 'utf8'));
const grammarText = JSON.stringify(grammar);
for (const keyword of ['module', 'import', 'using', 'extern', 'artifact', 'enum', 'namespace']) {
  if (!grammarText.includes(keyword)) fail(`grammar missing keyword: ${keyword}`);
}
for (const requiredGrammarScope of [
  'meta.declaration.variable.static-array.simple',
  'meta.declaration.function.static-array-return.simple',
  'constant.numeric.array-size.simple',
  'punctuation.definition.array-size.begin.simple',
  'punctuation.definition.array-size.end.simple',
  'storage.modifier.type.simple',
  'meta.declaration.namespace.simple'
]) {
  if (!grammarText.includes(requiredGrammarScope)) fail(`grammar missing complex-type hover scope: ${requiredGrammarScope}`);
}
const declarationPatterns = new Map((grammar.repository?.declarations?.patterns ?? []).map((pattern) => [pattern.name, pattern.match]));
for (const [name, sample] of [
  ['meta.declaration.namespace.simple', 'Raylib :: namespace'],
  ['meta.declaration.aggregate.simple', 'Packet :: data'],
  ['meta.declaration.variable.simple', 'colors :: Color[]'],
  ['meta.declaration.variable.simple', 'grid :: Color[][]'],
  ['meta.declaration.variable.simple', 'buffers : f32{10000}[]'],
  ['meta.declaration.variable.simple', 'texture : Texture2D'],
  ['meta.declaration.variable.simple', 'image : Image*'],
  ['meta.declaration.variable.simple', 'imagePtr : Image**'],
  ['meta.declaration.variable.simple', 'box : Box<Color>'],
  ['meta.declaration.variable.proc-type.simple', 'callback : fn i32 (value : i32)'],
  ['meta.declaration.variable.static-array.simple', 'posX : f32{10000}'],
  ['meta.declaration.function.simple', 'makeColors : Color[] (count : i32)'],
  ['meta.declaration.function.simple', 'makeGrid : Color[][] ()'],
  ['meta.declaration.function.simple', 'makePtr : Image** ()'],
  ['meta.declaration.function.simple', 'makeBox : Box<Color> ()'],
  ['meta.declaration.function.static-array-return.simple', 'makeBuffer : f32{10000} ()']
]) {
  const pattern = declarationPatterns.get(name);
  if (!pattern) fail(`grammar declaration pattern missing: ${name}`);
  if (!new RegExp(pattern).test(sample)) fail(`grammar pattern ${name} does not match canonical type syntax: ${sample}`);
}

const extensionText = fs.readFileSync(path.join(root, pkg.main), 'utf8');
if (!extensionText.includes('svm')) fail('extension must discover and invoke svm');
if (!extensionText.includes('registerTaskProvider')) fail('compiled extension must register Simple task provider');
if (!extensionText.includes('SIMPLE_LSP_TRACE')) fail('compiled extension must pass trace environment to svm');
if (!extensionText.includes('cliInputArg')) fail('compiled extension must use module-aware CLI input arguments');
if (!extensionText.includes('commandCwd')) fail('compiled extension must run svm from the document directory');
if (!extensionText.includes("commandWorks('svm')")) fail('extension must discover PATH svm');
for (const forbidden of ["'Compiler', 'bin'", "'Compiler', 'build'", "'build', 'bin'"]) {
  if (extensionText.includes(forbidden)) fail(`distributed extension must not probe workspace-local compiler path: ${forbidden}`);
}
if (extensionText.includes('simple.lspPath')) fail('compiled extension still references obsolete simple.lspPath');
console.log('extension manifest ok');
