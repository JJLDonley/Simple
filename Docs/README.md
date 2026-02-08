# Simple Documentation

This folder is the source of truth for the current Simple project state.

## Build Requirements

- C++17 compiler
- `libffi` development package
- Git + a shell environment

Build modes:

- Default mode: requires CMake 3.16+
- Legacy mode (`--legacy` on Linux/macOS): does not require CMake

Platform notes:

- Linux:
  - default: `cmake`, `g++`/`clang++`, `pkg-config`, `libffi-dev`
  - legacy: `g++`/`clang++`, `ar`, `libffi-dev`
- macOS:
  - default: `cmake`, LLVM or Apple Clang, `pkg-config`, `libffi` (Homebrew)
  - legacy: Apple Clang or LLVM toolchain, `libffi` (Homebrew)
- Windows:
  - `build_windows` is CMake-based (no legacy mode)
  - install CMake + MSVC (or equivalent C++ toolchain), run via Git Bash/MSYS/Cygwin
  - install `libffi` via `vcpkg` and pass vcpkg toolchain args to `build_windows --cmake-arg ...`

## Install

Authoritative scripts (from repo root):

- Linux: `./build_linux`
- macOS: `./build_macos`
- Windows: `./build_windows`

Each script performs:

1. Source build (CMake)
2. Package creation under `dist/`
3. Installation to default user-local paths

Defaults:

- default version: `v0.02`
- Linux/macOS install prefix: `~/.simple`
- Linux/macOS linked binaries: `~/.local/bin`
- Windows install prefix: `%LOCALAPPDATA%/Simple`
- Windows copied binaries: `%LOCALAPPDATA%/Simple/bin`

Common options:

- `--tests` to build and run tests (default is no tests)
- `--version <name>` package/install version tag
- `--legacy` (Linux/macOS) to use old non-CMake build flow
- `--skip-build`, `--skip-release`, `--skip-install`
- `--prefix <dir>`, `--bin-dir <dir>` to override install destinations
- `--no-link` (Linux/macOS) or `--no-link` (Windows) to skip binary links/copies

Legacy mode note:

- `--legacy` is supported in `build_linux` and `build_macos` only.
- `build_windows` remains CMake-based.

After install:

- build scripts now print a PATH check.
- if `simple` is discoverable, scripts print `simple --version` (or `simple -v`) output.
- CLI supports both `simple --version` and `simple -v`.

System-wide install examples:

- Linux: `sudo ./build_linux --prefix /opt/simple --bin-dir /usr/local/bin`
- macOS: `sudo ./build_macos --prefix /opt/simple --bin-dir /usr/local/bin`
- Windows: `./build_windows --prefix \"C:/Program Files/Simple\" --bin-dir \"C:/Program Files/Simple/bin\" --add-path`

## Authority Policy

High-level module docs are authoritative for their module:

- `Byte.md` is authoritative for SBC/loader/verifier behavior.
- `VM.md` is authoritative for runtime and FFI behavior.
- `IR.md` is authoritative for SIR/lowering behavior.
- `CLI.md` is authoritative for command behavior.
- `Lang.md` is authoritative for language syntax/semantics.
- `StdLib.md` is authoritative for reserved imports/core module surface.

`Docs/legacy/` exists as migration/reference material only and is not authoritative.

## Start Here

- `Docs/README.md`
  - Documentation map and ownership.
- `Docs/Modules.md`
  - Project architecture map and module boundaries.
- `Docs/Implementation.md`
  - Current implementation plan and freeze checklist.
- `Docs/Sprint.md`
  - Chronological engineering log.

## Core Runtime + Compiler Docs

- `Docs/Byte.md`
  - SBC format, loader, verifier, and compatibility policy.
- `Docs/VM.md`
  - Runtime execution model, GC/JIT status, and ABI/runtime constraints.
- `Docs/IR.md`
  - SIR text/lowering contract and IR compiler behavior.
- `Docs/CLI.md`
  - User-facing CLI behavior and command contracts.
- `Docs/Lang.md`
  - Language syntax and semantics.
- `Docs/StdLib.md`
  - Reserved imports and core library surface.

## Legacy Reference Docs

Legacy design references are kept in `Docs/legacy/`.

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
