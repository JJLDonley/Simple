#!/usr/bin/env node
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
const fail = (message) => {
  console.error(message);
  process.exit(1);
};

if (pkg.main !== './out/extension.js') fail('package main must point at compiled TypeScript output');
if (!fs.existsSync(path.join(root, pkg.main))) fail(`compiled extension missing: ${pkg.main}`);
if (pkg.contributes?.configuration?.properties?.['simple.lspPath']) fail('simple.lspPath is obsolete; use simple.compilerPath');
if (!pkg.contributes?.configuration?.properties?.['simple.compilerPath']) fail('simple.compilerPath setting missing');

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
  'simple.configureCompilerPath'
]) {
  if (!commands.has(command)) fail(`command missing: ${command}`);
}

const extensionText = fs.readFileSync(path.join(root, pkg.main), 'utf8');
if (!extensionText.includes('svm')) fail('extension must discover and invoke svm');
if (extensionText.includes('simple.lspPath')) fail('compiled extension still references obsolete simple.lspPath');
console.log('extension manifest ok');
