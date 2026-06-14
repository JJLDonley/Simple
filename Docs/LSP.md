# Simple LSP

The Simple language server provides editor features for `.simple` source files over stdio JSON-RPC.

## Table of contents

- [Starting the server](#starting-the-server)
- [Supported editor features](#supported-editor-features)
- [Diagnostics](#diagnostics)
- [Workspace model](#workspace-model)
- [Completion and signature help](#completion-and-signature-help)
- [Semantic tokens and highlighting](#semantic-tokens-and-highlighting)
- [Not supported yet](#not-supported-yet)
- [Tests](#tests)

## Starting the server

```txt
simple lsp
svm lsp
```

The server reads JSON-RPC messages from stdin and writes responses/notifications to stdout.

## Supported editor features

Current behavior includes:

- initialize/shutdown/exit
- document open/change/close
- incremental document sync with version guards
- diagnostics from the Simple language validator
- hover
- definition and declaration
- references
- document symbols
- completion
- signature help
- rename and prepareRename
- code actions
- semantic tokens

VS Code and Zed syntax-highlighting baselines exist under the editor extension directories.

## Diagnostics

The language server reuses the compiler pipeline for parse and validation diagnostics. It does not implement separate semantic rules. Compiler diagnostics are converted to LSP diagnostics with source ranges and messages.

## Workspace model

The server currently targets a single-workspace model. Multi-root workspace indexing is not part of the current supported behavior.

## Completion and signature help

Completion and signature help are based on the parsed/validated source model and known standard-library/reserved APIs. Signature help includes current IO formatting behavior and cast-call syntax where supported.

## Semantic tokens and highlighting

Semantic tokens provide structured highlighting for editor clients. TextMate grammar support is available as a fallback/baseline for editor packages.

## Not supported yet

- formatting
- broad refactor code actions
- multi-root indexing
- advanced cancellation inside every long-running operation

## Tests

LSP behavior is covered by `Tests/tests/test_lsp.cpp` and shared diagnostic bridge tests.
