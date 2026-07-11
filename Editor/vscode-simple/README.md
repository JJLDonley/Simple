# Simple VS Code Extension

VS Code support for the Simple language. The extension starts the real `svm lsp` server and adds editor commands that call the real `svm` CLI.

## Features

- Starts `svm lsp` over stdio with `vscode-languageclient`
- Registers `.simple` language id (`simple`)
- Provides TextMate grammar fallback highlighting
- Provides bracket/comment language configuration
- Adds Simple command palette, editor-title, and editor-context commands
- Discovers `svm` from `simple.compilerPath`, bundled binaries, workspace build outputs, then `PATH`

## Settings

- `simple.compilerPath`: optional path to `svm`; empty means auto-discover
- `simple.lspArgs`: args used to launch LSP, default `[
  "lsp"
]`

## Commands

- `Simple: Check Current File` -> `svm check <file>`
- `Simple: Run Current File` -> `svm run <file>`
- `Simple: Run Current File With JIT` -> `svm run <file> -jit --jit-stats`
- `Simple: Build Current File` -> `svm build <file>`
- `Simple: Compile Current File` -> `svm compile <file>`
- `Simple: Emit SIR` -> `svm emit -ir <file> --out <file.sir>`
- `Simple: Emit SBC` -> `svm emit -sbc <file> --out <file.sbc>`
- `Simple: Restart Language Server`
- `Simple: Show Language Server Output`
- `Simple: Show svm Version`
- `Simple: Show svm Help`
- `Simple: Configure Compiler Path`

The extension never invokes the `simple` runtime stub for compiler/editor commands.

## Local Development

```bash
npm ci
npm run validate
```

Press `F5` from this folder to start an Extension Development Host.

## Package and Install

```bash
npm run package -- --out simple-vscode.vsix
code --install-extension simple-vscode.vsix --force
```

Or use:

```bash
npm run install:vsix
```

after packaging.

## CI Packaging

- GitHub Actions workflow: `.github/workflows/vscode-extension.yml`
- CI runs `npm ci`, `npm run validate`, and packages `simple-vscode.vsix`.
- Manual dispatch can optionally attach the VSIX and checksum to an existing release tag via `tag_name` input.
