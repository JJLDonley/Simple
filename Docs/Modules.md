# Simple Module Map

This file defines ownership boundaries across the stack.

## End-To-End Flow

1. `Simple::Lang` (`.simple` parser/validator)
2. `Simple::IR` (`SIR` parser/lowering)
3. `Simple::Byte` (`SBC` load/verify)
4. `Simple::VM` (runtime execution)
5. `Simple::CLI` (user command orchestration)

## Ownership

### Simple::Lang

- language grammar and AST
- type validation and mutability checks
- import/extern semantics
- SIR emission

### Simple::IR

- SIR parsing
- label/fixup resolution
- lowering to SBC tables + code bytes

### Simple::Byte

- SBC binary contract
- loader structural validation
- verifier control-flow/type-stack invariants

### Simple::VM

- interpreter execution
- stack/frame/heap model
- core import dispatch
- DLL ABI call path (`DL`)

### Simple::CLI

- `run/check/build/compile/emit/lsp`
- source/IR/runtime pipeline wiring
- user-facing diagnostics and command behavior

### Tests

- regression coverage for Lang/IR/Byte/VM/LSP/CLI interactions
- positive and negative behavior contracts

## Canonical Docs

- Language: `Docs/Lang.md`
- Stdlib/import APIs: `Docs/StdLib.md`
- Bytecode: `Docs/Byte.md`
- IR: `Docs/IR.md`
- VM runtime: `Docs/VM.md`
- CLI behavior: `Docs/CLI.md`
