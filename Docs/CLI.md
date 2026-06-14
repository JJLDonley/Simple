# Simple CLI

The Simple command line tools compile, check, emit, build, run, and serve language tooling for Simple programs.

## Table of contents

- [Tool names](#tool-names)
- [Common options](#common-options)
- [Commands](#commands)
- [Input types](#input-types)
- [Command behavior](#command-behavior)
- [Diagnostics](#diagnostics)
- [Imports in CLI workflows](#imports-in-cli-workflows)
- [Build scripts](#build-scripts)

## Tool names

The same command implementation supports different compatibility modes by executable name:

- `svm`: primary compiler/runtime command. Accepts `.simple`, `.sir`, and `.sbc` inputs.
- `simple`: user-facing compatibility command focused on `.simple` workflows.
- `simplevm`: VM/developer compatibility command. Accepts `.simple`, `.sir`, and `.sbc`; build/compile default to `.sbc` output.

## Common options

```txt
--help, -h, help
--version, -v, version
```

Help prints command usage. Version prints the Simple tool version.

## Commands

```txt
run      compile/load and execute input
check    validate input without executing
build    produce an output artifact
compile  alias-style build workflow
emit     print or write intermediate output
lsp      start the stdio language server
```

## Input types

### `.simple`

A Simple source file is parsed, validated, emitted to SIR, lowered to SBC, and then either executed or written depending on the command.

### `.sir`

A textual SIR module is parsed/lowered to SBC and then optionally executed.

### `.sbc`

An SBC bytecode file is loaded, verified, and optionally executed.

## Command behavior

### `run`

Runs the selected input through the necessary pipeline and executes it in the VM.

### `check`

Validates syntax/semantics or bytecode structure without execution. For `.simple`, this means language validation; for `.sbc`, loader/verifier checks apply.

### `emit`

Emits intermediate output. Current workflows use it primarily for SIR/SBC inspection.

### `build` / `compile`

Builds an output artifact. In `simplevm` compatibility mode the default output style is `.sbc`; in `svm` workflows executable-stub behavior is selected unless the output path ends in `.sbc`.

### `lsp`

Starts the JSON-RPC language server over stdio. See `Docs/LSP.md`.

## Diagnostics

CLI diagnostics use stable `error[Exxxx]:` prefixes where command-level errors apply. Compiler diagnostics are produced by the language pipeline and rendered for terminal output.

Examples of command-level failures include missing input, unsupported extension for the selected compatibility mode, bad arguments, load/verify failure, and runtime failure.

## Imports in CLI workflows

When compiling `.simple`, the CLI resolves local imports, project-root imports, module-map entries, and reserved standard-library imports. Generated `simple.modules` files are build artifacts and should not be committed.

## Build scripts

Platform build scripts package the CLI/runtime artifacts and copy documentation into release staging directories where applicable.
