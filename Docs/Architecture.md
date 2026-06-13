# Architecture Ownership

## Owned files
- VM runtime: `VM/include/**`, `VM/src/**`
- Language pipeline: `Lang/include/**`, `Lang/src/**`
- CLI: `CLI/include/**`, `CLI/src/**`
- LSP: `LSP/include/**`, `LSP/src/**`
- Tests: `Tests/tests/**`

## Forbidden dependencies
- VM runtime must not depend on CLI or LSP.
- RAST must not perform TAST type checking.
- TAST must not own import graph construction.
- CLI and LSP must not duplicate compiler semantic rules.

## Public API
- VM: `Simple::VM::ExecuteModule`, native registry APIs.
- Lang: Lexer, CAST, AST, RAST, TAST, IRB, IRE phase headers.
- CLI: command, import, build, and diagnostic helper headers.
- LSP: `RunLspServer` and diagnostic bridge APIs.

## Tests
- VM split tests live under `Tests/tests/vm/`.
- Language split tests live under `Tests/tests/lang/`.
- CLI split tests live under `Tests/tests/cli/`.
- LSP tests live in `Tests/tests/test_lsp.cpp` until their split phase.
