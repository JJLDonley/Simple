# Simple Module Map

This document defines module boundaries and ownership for the current project state.

## Compiler + Runtime Pipeline

1. `Simple::Lang`
   - Parses and validates `.simple` source.
   - Emits SIR text.
2. `Simple::IR`
   - Parses/validates SIR.
   - Lowers SIR to SBC module bytes.
3. `Simple::Byte`
   - Loads SBC binaries.
   - Verifies structural and type safety.
4. `Simple::VM`
   - Executes verified SBC.
   - Handles heap, GC, imports, and dynamic DL dispatch.
5. `Simple::CLI`
   - User entrypoints (`simple`, `simplevm`) for check/build/run/emit.

## Module Responsibilities

### Simple::Lang
- Source language grammar and semantic validation.
- Type checking, mutability checks, import/extern checks.
- SIR emission, including globals and extern import signatures.

### Simple::IR
- SIR text parser and metadata resolution.
- Lowering to SBC tables + code sections.
- SIR diagnostics and fixture support.

### Simple::Byte
- SBC header/section/table parsing.
- Loader bounds/alignment validation.
- Verifier stack/type/control-flow validation.

### Simple::VM
- Interpreter execution engine.
- Heap object model and GC root scanning.
- Import resolver handling (`core.os`, `core.fs`, `core.log`, `core.dl`).
- Dynamic FFI dispatch (`Core.DL` manifests), including by-value struct marshalling.

### Simple::CLI
- User command UX (`run`, `build`, `check`, `emit`).
- Front-end orchestration for `.simple` and `.sir`.
- Build/link mode controls for runtime embedding.

### Simple::Tests
- Unit tests for core/ir/jit/lang suites.
- `.simple` and `.sir` fixtures (positive and negative).
- Regression coverage for runtime traps and verifier failures.

## Current Stability Notes

- Interpreter path is the stability baseline.
- JIT exists but should be treated as lower-stability than interpreter for alpha freeze decisions.
- Dynamic DL FFI supports exact extern signatures, including structs and pointers, with explicit unsupported recursive-struct limits.

## Ownership Docs

- Format + verification: `Simple/Docs/Byte.md`
- Runtime + ABI behavior: `Simple/Docs/VM.md`
- IR contract: `Simple/Docs/IR.md`
- CLI contract: `Simple/Docs/CLI.md`
- Language contract: `Simple/Docs/Lang.md`
- Core imports/stdlib contract: `Simple/Docs/StdLib.md`
