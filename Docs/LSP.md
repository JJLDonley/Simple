# Simple::LSP (Authoritative)

## Scope

`Simple::LSP` defines editor protocol behavior and highlighting for `.simple` sources.

Primary goals:

- provide a fully functional Language Server Protocol implementation for Simple
- provide syntax highlighting that works in common editors (starting with VS Code)
- keep diagnostics and symbol semantics aligned with `Simple::Lang`

## Architecture

### Server Model

- process model: stdio JSON-RPC LSP server (`simple lsp`)
- single workspace first, multi-root later
- incremental document sync (`textDocument/didChange`)
- authoritative parse/validate path reuses `Lang` front-end pipeline

### Core Data Pipeline

1. document open/change event updates in-memory source snapshot
2. debounce + parse/validate source
3. publish diagnostics with ranges and severity
4. update symbol/type index for language features
5. answer requests (hover/completion/definition/references/tokens) from indexed state

## Protocol Contract (v1)

### Must-Have Methods

- `initialize` / `initialized` / `shutdown` / `exit`
- `textDocument/didOpen`
- `textDocument/didChange`
- `textDocument/didClose`
- `textDocument/publishDiagnostics`
- `textDocument/hover`
- `textDocument/definition`
- `textDocument/declaration`
- `textDocument/documentHighlight`
- `textDocument/references`
- `textDocument/documentSymbol`
- `textDocument/completion`
- `textDocument/semanticTokens/full`

### Nice-to-Have (post-v1)

- `textDocument/signatureHelp`
- `textDocument/rename`
- `textDocument/codeAction`
- `workspace/symbol`

## Semantic Features

### Diagnostics

- reuse existing rust-style diagnostic formatter behavior and source span extraction
- map parser/validator errors to LSP diagnostics with stable codes (`E0001`, etc.)
- include related information where possible (for example duplicate symbol sites)
- quick-fix code actions for undeclared identifiers insert declarations after top-of-file import headers (instead of always line 0)
- undeclared-identifier quick-fix now infers numeric declaration type for simple assignments (`i32` vs `f64`)

### Symbols and Navigation

- declarations: functions, variables, modules, artifacts, enums, externs
- local scopes for function/block-level identifiers
- import-aware resolution for project-local files and reserved modules
- hover enriches reserved imported alias members with callable signatures (for example `OS.args_get(index) -> string`)
- rename/prepareRename reject reserved imported module member symbols (for example `OS.args_get`, `IO.println`) to prevent invalid API rewrites
- document highlights classify declaration + assignment targets as write highlights and other uses as read highlights

### Completion

- keywords and builtins
- in-scope identifiers
- member completion (`Module.member`, `Artifact.member`, `IO.println`, etc.)
- reserved module member completion for imported modules/aliases (for example `import "Core.DL" as DL` enables `DL.call_i32`, `DL.open`, ...)
- import path completion inside `import "..."` strings:
  - reserved module names (`IO`, `Math`, `Time`, `File`, `Core.*`)
  - open-document `.simple` module stems

### Signature Help (Current)

- trigger characters: `(`, `,`, `@` (cast-call friendly)
- builtins:
  - `IO.print(value)` / `IO.println(value)`
  - `IO.print(format, values...)` / `IO.println(format, values...)` for `{}` format-placeholder calls
- IO aliases imported via `as` keep the same overloaded signature-help behavior (for example `Out.println(format, values...)`)
- language casts:
  - `@T(value)` cast-call signature help (for example `@i32(value)`)
- reserved imported modules (including `as` aliases):
  - signature help resolves known reserved-module members such as `OS.args_get(index)`, `DL.open(path)`, `FS.read(fd, buffer, count)`, etc.
  - overload-aware signatures are emitted where applicable (for example `DL.open(path)` and `DL.open(path, manifest)`)
- local function-style declarations:
  - resolves `name : type (params)` declarations to signature payloads with per-parameter labels

## Syntax Highlighting Plan

Deliver both layers:

1. semantic tokens from LSP:
   - token kinds: keyword, type, function, variable, parameter, property, enumMember, namespace, string, number, operator
   - modifiers: declaration, readonly, defaultLibrary
2. grammar fallback (TextMate):
   - VS Code extension grammar for immediate non-LSP highlighting
   - keeps basic highlighting available if server is unavailable

## Repository Plan

Planned layout:

- `LSP/` core server implementation
- `LSP/include/` shared protocol/types interfaces
- `LSP/src/` JSON-RPC, document store, feature handlers
- `Editor/vscode-simple/` VS Code extension:
  - client launcher for `simple lsp`
  - TextMate grammar
  - language configuration (`comments`, `brackets`, `autoClosingPairs`)

### VS Code Setup (Implemented Baseline)

- extension scaffold lives under `Editor/vscode-simple/`
- extension currently provides:
  - language registration for `.simple`
  - stdio LSP client launch (`simple lsp`)
  - TextMate grammar fallback highlighting
  - language configuration for comments/brackets/autoclose
- extension runtime settings:
  - `simple.lspPath`
  - `simple.lspArgs`

### VS Code Packaging + Release Automation

- CI workflow: `.github/workflows/vscode-extension.yml`
- behavior:
  - on `main` pushes that touch `Editor/vscode-simple/**`, build `simple-vscode.vsix` and publish as workflow artifact
  - on manual dispatch, if `tag_name` is provided, attach VSIX + checksum to that GitHub release tag
- packaging command used by CI:
  - `npx --yes @vscode/vsce package --out simple-vscode.vsix`

## Milestones

### M1: Functional Server Skeleton

- `simple lsp` command launches stdio server
- initialize/shutdown lifecycle implemented
- open/change/close document management
- diagnostics publish on save/change

### M2: Navigation + Completion

- hover
- go-to-definition
- references
- document symbols
- completion

### M3: Highlighting

- semantic tokens full-document implementation
- VS Code TextMate grammar + language config

### M4: Stability

- cancellation + request ordering handling
- debouncing/perf for large files
- regression tests for diagnostics and symbol resolution

## Test Plan

- protocol tests for JSON-RPC framing and initialize contract
- integration tests for diagnostics and definition/references across imported files
- semantic token snapshot tests for representative fixtures
- editor smoke test checklist (open, edit, diagnostics, completion, navigation, highlighting)

## Out of Scope (v1)

- formatting engine
- refactor-heavy code actions
- multi-workspace indexing
