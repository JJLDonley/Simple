# Simple LSP

`Simple::LSP` owns editor protocol behavior for `.simple` sources.

## Owned files

- Server: `LSP/src/lsp_server.cpp`
- Diagnostic bridge: `LSP/include/diagnostic_bridge.h`, `LSP/src/diagnostic_bridge.cpp`
- Editor packages: `Editor/vscode-simple/`, `Editor/zed-simple/`

## Supported

- Stdio JSON-RPC server through `simple lsp` / `svm lsp`.
- Single-workspace indexing.
- Incremental document sync with version guards.
- Parse/validate diagnostics from the `Simple::Lang` pipeline.
- Hover, definition, declaration, references, document symbols.
- Completion, signature help, rename/prepareRename, code actions.
- Semantic tokens and TextMate grammar fallback.
- VS Code and Zed syntax-highlighting baselines.

## Not supported

- Formatting engine.
- Refactor-heavy code actions.
- Multi-root workspace indexing.

## Protocol contract

Implemented/covered methods include initialization, shutdown/exit, document open/change/close, diagnostics, hover, definition/declaration/references, document symbols, completion, signature help, semantic tokens, rename/prepareRename, and code actions.

LSP diagnostics are produced by the language front-end and converted to protocol JSON by the diagnostic bridge.

## Forbidden dependencies

- LSP must not duplicate parser/type-checker semantics.
- LSP must not own CLI terminal rendering.
- Long-running behavior should remain cancellation-aware where request dispatch supports it.

## Tests

LSP coverage lives in:

- `Tests/tests/test_lsp.cpp`
- diagnostic bridge coverage shared with language/CLI tests
