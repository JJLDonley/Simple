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

## Notes

This extension expects the CLI to support the `lsp` command and be available on your `PATH` (or configured via `simple.lspPath`).
