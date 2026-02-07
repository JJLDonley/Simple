# Simple::CLI

## Scope

`Simple::CLI` provides user-facing command entrypoints for checking, building, emitting, and running programs.

## Current State

Implemented:

- `simple` front-end for `.simple` workflows.
- `simplevm` front-end for VM/SIR workflows.
- Commands: `run`, `build`, `check`, `emit`.
- Diagnostic format with line/column context.
- Runtime embedding modes used by build tooling.

## Alpha Contract (Intended)

- Command behavior and error formatting are stable.
- `.simple` and `.sir` compile/run flows are deterministic from repository root.

## Known Limits / Explicit Constraints

- CLI is not a package manager.
- LSP/editor integration is out of scope for CLI surface.

## Primary Files

- `Simple/CLI/src/main.cpp`
- `Simple/build.sh`

## Verification Commands

- `./Simple/build.sh --suite lang`
- `./Simple/build.sh --suite all`
