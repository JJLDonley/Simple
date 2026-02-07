# Simple Documentation

This folder is the source of truth for the current Simple project state.

## Authority Policy

High-level module docs are authoritative for their module:

- `Byte.md` is authoritative for SBC/loader/verifier behavior.
- `VM.md` is authoritative for runtime and FFI behavior.
- `IR.md` is authoritative for SIR/lowering behavior.
- `CLI.md` is authoritative for command behavior.
- `Lang.md` is authoritative for language syntax/semantics.
- `StdLib.md` is authoritative for reserved imports/core module surface.

`Simple/Docs/legacy/` exists as migration/reference material only and is not authoritative.

## Start Here

- `Simple/Docs/README.md`
  - Documentation map and ownership.
- `Simple/Docs/Modules.md`
  - Project architecture map and module boundaries.
- `Simple/Docs/Implementation.md`
  - Current implementation plan and freeze checklist.
- `Simple/Docs/Sprint.md`
  - Chronological engineering log.

## Core Runtime + Compiler Docs

- `Simple/Docs/Byte.md`
  - SBC format, loader, verifier, and compatibility policy.
- `Simple/Docs/VM.md`
  - Runtime execution model, GC/JIT status, and ABI/runtime constraints.
- `Simple/Docs/IR.md`
  - SIR text/lowering contract and IR compiler behavior.
- `Simple/Docs/CLI.md`
  - User-facing CLI behavior and command contracts.
- `Simple/Docs/Lang.md`
  - Language syntax and semantics.
- `Simple/Docs/StdLib.md`
  - Reserved imports and core library surface.

## Legacy Reference Docs

Legacy design references are kept in `Simple/Docs/legacy/`.

These are historical references, not the primary source of truth for current behavior.

## Documentation Rules

- If code behavior changes, update the module doc and append a sprint log item in the same change.
- Keep one source of truth per topic:
  - Language: `Lang.md`
  - Runtime behavior: `VM.md`
  - Bytecode/verification: `Byte.md`
  - IR text/lowering: `IR.md`
  - CLI behavior: `CLI.md`
  - Stdlib/import surface: `StdLib.md`
- Treat `Implementation.md` as plan status and `Sprint.md` as execution history.

## Alpha Freeze Documentation Checklist

- Module docs reflect implemented behavior, not aspirational design.
- Known limitations are explicit and testable.
- Cross-doc terminology is consistent (`SBC`, `SIR`, `extern`, `Core.DL`, `artifact`, `pointer`).
- Commands in docs are runnable from repository root.
