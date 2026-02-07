# Simple::CLI (Authoritative)

## Scope

`Simple::CLI` defines user-facing command surfaces for compilation and execution workflows.

## User Interface Design

### Command Families

- `simple` (primary user-facing CLI)
- `simplevm` (compatibility alias for mixed `.simple/.sir/.sbc` workflows)

### Primary Actions

- `check`: parse/validate without full run
- `build`: compile outputs (`simple build` defaults to executable output)
- `run`: compile/execute pipeline
- `emit`: emit intermediate/bytecode artifacts
- `lsp`: reserved command (currently returns not implemented)

### UX Contract

- deterministic exit codes
- deterministic error header format (`error[E0001]: ...` style)
- source location in diagnostics when available
- stable behavior from repository root paths

## CLI Pipeline Design

1. detect input mode (`.simple`, `.sir`, compiled module)
2. invoke compiler/lowering phase(s)
3. run loader/verifier
4. execute runtime path (for `run`)
5. print diagnostics and return command status

## Build/Runtime Interface Notes

- `Simple/build.sh` is the canonical dev/test orchestration entrypoint.
- runtime/static-shared linking behavior is controlled by build scripts + CLI integration.
- `simple build <file.simple>` defaults to embedding SBC+runtime into an executable.
- `simple build <file.simple> --out <name.sbc>` forces SBC bytecode output.

## Constraints

- CLI is not a package manager.
- LSP/editor protocol is out of scope for CLI contract.

## Files

- `Simple/CLI/src/main.cpp`
- `Simple/build.sh`

## Verification Commands

- `./Simple/build.sh --suite lang`
- `./Simple/build.sh --suite all`
