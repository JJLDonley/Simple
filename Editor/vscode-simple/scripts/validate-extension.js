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
if (!Array.isArray(pkg.contributes?.taskDefinitions) || !pkg.contributes.taskDefinitions.some((t) => t.type === 'simple')) fail('simple task definition missing');
if (!Array.isArray(pkg.contributes?.problemMatchers) || !pkg.contributes.problemMatchers.some((m) => m.name === 'simple-svm')) fail('simple problem matcher missing');
if (!Array.isArray(pkg.contributes?.menus?.['explorer/context']) || pkg.contributes.menus['explorer/context'].length === 0) fail('explorer context entries missing');

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
  'simple.toggleJitDefault'
]) {
  if (!commands.has(command)) fail(`command missing: ${command}`);
}

const extensionText = fs.readFileSync(path.join(root, pkg.main), 'utf8');
if (!extensionText.includes('svm')) fail('extension must discover and invoke svm');
if (!extensionText.includes('registerTaskProvider')) fail('compiled extension must register Simple task provider');
if (extensionText.includes('simple.lspPath')) fail('compiled extension still references obsolete simple.lspPath');
console.log('extension manifest ok');
