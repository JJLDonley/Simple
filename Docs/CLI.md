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
- `compile`: alias of `build` (Deno-style command shape)
- `run`: compile/execute pipeline
- `emit`: emit intermediate/bytecode artifacts
- `lsp`: launch stdio LSP server (`simple lsp`)

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

For `.simple` input paths, CLI also resolves non-reserved imports as local project files (recursive, importer-relative, extensionless `.simple` fallback).

## Build/Runtime Interface Notes

- `build.sh` is the canonical dev/test orchestration entrypoint.
- runtime/static-shared linking behavior is controlled by build scripts + CLI integration.
- `simple build <file.simple>` defaults to embedding SBC+runtime into an executable.
- `simple build <file.simple> --out <name.sbc>` forces SBC bytecode output.
- `simple compile ...` is equivalent to `simple build ...`.
- Embedded executable builds resolve runtime/include paths from either:
  - source layout (`VM/include`, `Byte/include`, `bin`), or
  - installed layout (`<prefix>/include/simplevm`, `<prefix>/lib`).

## Distribution Install Design

- Build release archive from source build outputs:
  - `./release.sh --version <tag> [--target <os-arch>]`
- Install for end users without rebuilding source:
  - `./install.sh --from-file ./dist/simple-<tag>-<target>.tar.gz --version <tag>`
  - or `./install.sh --url <https://...tar.gz> --version <tag>`
- Installer layout:
  - `${HOME}/.simple/<version>/bin/simple`
  - `${HOME}/.simple/<version>/lib/libsimplevm_runtime.{a,so}`
  - `${HOME}/.simple/<version>/include/simplevm/*.h`
  - `${HOME}/.simple/current` symlink to active version
  - `${HOME}/.local/bin/simple` and `${HOME}/.local/bin/simplevm` symlinks

## Constraints

- CLI is not a package manager.
- LSP/editor protocol is out of scope for CLI contract.

## Files

- `CLI/src/main.cpp`
- `build.sh`

## Verification Commands

- `./build.sh --suite lang`
- `./build.sh --suite all`
