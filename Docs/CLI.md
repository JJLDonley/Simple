# Simple CLI

`Simple::CLI` owns command-line parsing, user diagnostics, import/build orchestration, and command dispatch.

## Owned files

- Entry point: `CLI/src/main.cpp`
- Command contract/dispatch: `CLI/include/command_contract.h`, `CLI/src/command_contract.cpp`, `CLI/include/command_dispatch.h`, `CLI/src/command_dispatch.cpp`
- Build contract: `CLI/include/build_contract.h`, `CLI/src/build_contract.cpp`
- Import contract: `CLI/include/import_contract.h`, `CLI/src/import_contract.cpp`
- Diagnostic rendering: `CLI/include/diagnostic_render.h`, `CLI/src/diagnostic_render.cpp`

## Binaries

The code supports behavior by executable name:

- `svm`: primary compiler/runtime name; accepts `.simple`, `.sir`, and `.sbc`; `build`/`compile` default to executable stubs unless output ends in `.sbc`
- `simple`: compatibility user-facing name focused on `.simple` workflows
- `simplevm`: compatibility/developer VM name; accepts `.simple`, `.sir`, and `.sbc`; `build`/`compile` default to `.sbc`

## Commands

Implemented commands:

- `run`
- `check`
- `build`
- `compile`
- `emit`
- `lsp`
- `help`, `--help`, `-h`
- `version`, `--version`, `-v`

## Input modes

- `.simple`: parse/validate, emit SIR, compile to SBC, optionally execute
- `.sir`: lower/compile to SBC, optionally execute
- `.sbc`: load/verify, optionally execute

## Command behavior

- `check` validates without execution.
- `emit` writes requested intermediate output, especially SIR/SBC workflows.
- `build`/`compile` produce `.sbc` or executable stubs depending on binary/output mode.
- `run` executes the selected source/artifact through the pipeline.
- `lsp` starts the stdio LSP server.

## Diagnostics and exits

CLI renders structured compiler diagnostics for terminals. Compiler phases own semantic checks; CLI owns presentation and process exit behavior.

Common failures include missing input, wrong extension for compatibility modes, parse/validation errors, load/verify errors, and runtime errors.

## Forbidden dependencies

- CLI must not duplicate language semantic checks.
- CLI must not own LSP protocol formatting.
- CLI should use shared import/build contracts instead of embedding command-specific import graph logic.

## Tests

CLI coverage lives in:

- `Tests/tests/cli/test_cli_build.cpp`
- `Tests/tests/cli/test_cli_contract.cpp`
- `Tests/tests/cli/test_cli_diagnostics.cpp`
- `Tests/tests/cli/test_cli_imports.cpp`
- CLI portions of `Tests/tests/test_lang.cpp`
