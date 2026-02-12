# Simple Documentation (API)

This is the authoritative API overview for the Simple project. Each module doc follows the same contract-first format.

## Supported
- End-to-end pipeline: `.simple` -> `SIR` -> `SBC` -> VM execution.
- Strict typing across language, IR, bytecode, and VM.
- Deterministic validation with explicit diagnostics.
- Core reserved standard library modules via import mapping.
- CLI workflows for `run`, `check`, `build/compile`, `emit`, and `lsp`.
- LSP server with diagnostics, navigation, completion, and semantic tokens.
- C/C++ interop through `DL` with a strict ABI manifest contract.

## Not Supported
- Package manager ecosystem.
- Optimizing compiler pipeline (beyond the current interpreter + limited JIT scaffolding).
- AOT native backend.
- Advanced GC generations/tuning work.

## Planned
- Formal SBC compatibility/versioning policy.
- Explicit, tested SIR subset contract coverage for unsupported forms.
- CLI contract freeze with consistent exit code and error format guarantees.
- Expanded CI matrix and release gating.
- JIT maturity beyond current experimental tiering.

## Pipeline
1. `Simple::Lang` parses + validates `.simple`.
2. Emits `SIR` text.
3. `Simple::IR` lowers `SIR` -> `SBC`.
4. `Simple::Byte` loads + verifies `SBC`.
5. `Simple::VM` executes verified modules.

## Canonical Docs
- `Docs/Lang.md` - language syntax and semantics
- `Docs/StdLib.md` - reserved imports and runtime module APIs
- `Docs/IR.md` - SIR syntax, validation, lowering rules
- `Docs/Byte.md` - SBC format, loader, verifier contract
- `Docs/VM.md` - runtime model, heap/GC, imports, DL ABI
- `Docs/CLI.md` - CLI surface and command behavior
- `Docs/LSP.md` - editor/LSP behavior and feature coverage
- `Docs/Modules.md` - ownership boundaries across the stack
- `Docs/Implementation.md` - release plan and execution gates
- `Docs/Sprint.md` - change log and execution history

## Documentation Rules
- If behavior changes in code, update the matching module doc in the same change.
- Keep examples runnable and aligned with current syntax/CLI behavior.
- `Docs/legacy/` is reference-only, not authoritative.
