# Simple VS Code Extension

This extension wires VS Code to the `simple lsp` language server and provides TextMate syntax highlighting fallback for `.simple` files.

## Features

- Launches `simple lsp` via stdio using `vscode-languageclient`
- Registers `.simple` language id (`simple`)
- Provides TextMate grammar fallback highlighting
- Provides bracket/comment language configuration

## Settings

- `simple.lspPath`: path to `simple` CLI executable (default: `simple`)
- `simple.lspArgs`: args used to launch LSP (default: `["lsp"]`)

## Local Development

1. Open `Editor/vscode-simple` in VS Code.
2. Run `npm install`.
3. Press `F5` to start Extension Development Host.
4. Open a `.simple` file in the host window.

## Install From VSIX

1. Build the package:
   - `npx --yes @vscode/vsce package --out simple-vscode.vsix`
2. Install in VS Code:
   - `code --install-extension simple-vscode.vsix`

## CI Packaging

- GitHub Actions workflow: `.github/workflows/vscode-extension.yml`
- On pushes to `main` that touch `Editor/vscode-simple/**`, CI builds `simple-vscode.vsix` and uploads it as an artifact.
- Manual dispatch can optionally attach the VSIX and checksum to an existing release tag via `tag_name` input.

## Notes

This extension expects the CLI to support the `lsp` command and be available on your `PATH` (or configured via `simple.lspPath`).
