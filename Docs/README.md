# Simple Documentation Overview

This directory documents the current Simple compiler, language, bytecode, VM, CLI, LSP, and standard library contracts.

## Current Pipeline

```txt
.simple source
  -> Lang lexer/parser/validator
  -> SIR text
  -> IR lowerer/compiler
  -> SBC bytecode
  -> Byte loader/verifier
  -> VM interpreter
```

## Implemented

- End-to-end `.simple -> SIR -> SBC -> VM` pipeline.
- Strict language validation before bytecode generation.
- Textual SIR compiler layer.
- SBC binary format with loader and verifier.
- Deterministic interpreter runtime.
- Heap objects for strings, arrays, lists, artifacts, and closures.
- Reserved/core runtime imports.
- Dynamic library interop through declared extern metadata on supported platforms.
- CLI workflows for `run`, `check`, `build/compile`, `emit`, and `lsp`.
- LSP/editor baseline.
- Positive and negative language/runtime fixtures.

## In Progress

- Formal SIR contract and compatibility policy.
- Formal SBC compatibility/versioning policy.
- Complete opcode semantic metadata shared across compiler/verifier/VM.
- JIT maturity beyond instrumentation/scaffolding.
- Full generic/procedure-value/pointer feature hardening.
- Stable CLI exit-code and diagnostic-format contract.
- CI/release gate documentation.

## Future

- Package manager ecosystem.
- Optimizing compiler middle-end.
- AOT native backend.
- Full optimizing JIT.
- Advanced GC policy/tuning.
- Stable plugin/compiler extension APIs.
- Sandboxed untrusted bytecode execution.

## Canonical Docs

- `Docs/Language.md` - language syntax, semantics, imports, casts, mutability, and diagnostics
- `Docs/StdLib.md` - reserved imports and runtime module APIs
- `Docs/IR.md` - SIR/compiler contract
- `Docs/Byte.md` - SBC format, loader, verifier contract
- `Docs/VM.md` - runtime model, heap, imports, DL ABI
- `Docs/JIT.md` - optional JIT tiering and compiled-runner behavior
- `Docs/CLI.md` - command-line behavior
- `Docs/LSP.md` - editor/LSP behavior
- `Docs/TODO.md` - implementation and hardening checklist
- `Docs/Sprint.md` - change log and execution history

## Documentation Rules

- If behavior changes in code, update the matching module doc in the same change.
- Prefer `Implemented`, `In Progress`, and `Future` sections for status clarity.
- Keep examples runnable and aligned with current syntax/CLI behavior.
- `Docs/legacy/` is reference-only, not authoritative.
