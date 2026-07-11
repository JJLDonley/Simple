#!/usr/bin/env node
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const compilerRoot = path.resolve(root, '..', '..');
const versionPath = path.join(compilerRoot, 'VERSION');
const packagePath = path.join(root, 'package.json');

const rawVersion = fs.readFileSync(versionPath, 'utf8').trim();
const packageVersion = rawVersion.startsWith('v') ? rawVersion.slice(1) : rawVersion;
if (!/^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/.test(packageVersion)) {
  throw new Error(`VERSION is not a valid VSIX semver: ${rawVersion}`);
}

const pkg = JSON.parse(fs.readFileSync(packagePath, 'utf8'));
if (pkg.version === packageVersion) {
  console.log(`VSIX version already ${packageVersion}`);
  process.exit(0);
}
pkg.version = packageVersion;
fs.writeFileSync(packagePath, `${JSON.stringify(pkg, null, 2)}\n`);
console.log(`Updated VSIX version to ${packageVersion}`);
