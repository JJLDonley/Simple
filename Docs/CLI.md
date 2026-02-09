# Simple::CLI (Authoritative)

This document defines user-facing CLI behavior.

## Binaries

- `simple` (primary)
- `simplevm` (compatibility alias)

## Commands

- `run`
- `check`
- `build`
- `compile` (alias of `build`)
- `emit`
- `lsp`

## Version Flags

- `simple --version`
- `simple -v`
- `simple version`

## Input Modes

- `.simple`
- `.sir`
- `.sbc` (via VM path/tooling)

## Build/Install Scripts (Authoritative)

- Linux: `./build_linux`
- macOS: `./build_macos`
- Windows: `./build_windows`

Common options:

- `--tests`
- `--version <tag>`
- `--skip-build`
- `--skip-release`
- `--skip-install`
- `--prefix <dir>`
- `--bin-dir <dir>`

Legacy mode:

- Linux/macOS only: `--legacy`

Windows toolchain pass-through:

- repeatable `--cmake-arg <arg>`
- typically used for vcpkg toolchain and triplet args

## Import Resolution In CLI Workflows

For `.simple` entry files:

- reserved imports map to stdlib modules
- non-reserved imports resolve from importer-relative paths
- extensionless local imports can resolve `.simple`
- import cycles are rejected

## Diagnostics Contract

- deterministic non-zero exit on failure
- stable error header format (for example `error[E0001]: ...`)
- location-aware diagnostics when available

## Ownership

- CLI implementation: `CLI/src/main.cpp`
