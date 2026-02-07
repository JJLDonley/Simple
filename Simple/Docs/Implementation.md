# Simple Implementation Plan (Current)

This is the active project plan used for alpha freeze tracking.

## Project Goals

- Stable language -> IR -> bytecode -> runtime pipeline.
- Deterministic verification/runtime behavior.
- Documented and test-backed FFI/interop surface.

## v0.1 Non-Goals

- Package manager.
- AOT backend.
- Full optimizer pipeline.
- Full generational GC rollout.

## Module Status

### Simple::Byte

Docs:
- `Simple/Docs/Byte.md`

Status:
- Header/section/table parsing implemented.
- Verifier implemented and integrated into execution path.

Alpha checklist:
- [x] Strict loader bounds/alignment checks.
- [x] Verifier stack/type/control-flow checks.
- [ ] Freeze compatibility policy and versioning note in release docs.

### Simple::VM

Docs:
- `Simple/Docs/VM.md`

Status:
- Interpreter baseline stable.
- Heap + GC in use.
- Dynamic DL signature dispatch with by-value struct marshalling implemented.

Alpha checklist:
- [x] Runtime trap diagnostics.
- [x] Import handling for core modules.
- [x] Dynamic FFI exact-signature dispatch.
- [ ] Decide and document alpha JIT stability posture.
- [ ] Remove or clearly mark non-alpha placeholder paths.

### Simple::IR

Docs:
- `Simple/Docs/IR.md`

Status:
- SIR parser/validator/lowering implemented.

Alpha checklist:
- [x] Metadata + lowering pipeline.
- [x] Error reporting with line context.
- [ ] Freeze supported SIR subset in docs.

### Simple::Lang

Docs:
- `Simple/Docs/Lang.md`
- `Simple/Docs/StdLib.md`

Status:
- Lexer/parser/validator/SIR emission implemented.
- Extern/import and `Core.DL` manifest flow implemented.

Alpha checklist:
- [x] Type checking and mutability validation.
- [x] Global init function flow.
- [x] Pointer and artifact extern ABI support.
- [ ] Finalize and publish explicit "supported vs not-supported" language subset.

### Simple::CLI

Docs:
- `Simple/Docs/CLI.md`

Status:
- `simple` and `simplevm` command flows implemented.

Alpha checklist:
- [x] check/build/run/emit behavior.
- [x] Diagnostics format consistency.
- [ ] Final command/API freeze note for alpha release.

### Simple::Tests

Docs:
- `Simple/Docs/Sprint.md`

Status:
- Core/IR/JIT/Lang suites and fixture runs implemented.

Alpha checklist:
- [x] Regression suites passing on repository baseline.
- [ ] Add/lock alpha smoke suite profile.
- [ ] Publish required CI matrix for alpha gate.

## Alpha Release Gate

All must be true:

1. `./Simple/build.sh --suite all` passes.
2. Module docs match actual runtime/compiler behavior.
3. Known constraints are explicit in docs.
4. Sprint log includes all behavior changes and test evidence.
5. Release notes include compatibility and known limitations.

## Test Commands

- `./Simple/build.sh --suite core`
- `./Simple/build.sh --suite ir`
- `./Simple/build.sh --suite jit`
- `./Simple/build.sh --suite lang`
- `./Simple/build.sh --suite all`
