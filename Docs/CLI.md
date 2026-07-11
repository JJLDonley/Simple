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

- `svm`: the compiler/tooling/runtime command. It owns `run`, `check`, `build`, `compile`, `emit`, and `lsp`.
- `simple`: runtime-stub name only. It is not a compiler and does not expose compiler commands.

The root `bin/` directory should contain only these two files after `./build.sh` or `build.bat`.

## Common options

```txt
--help, -h, help
--version, -v, version
-int, --interpreter
-jit
--jit-stats
```

Help prints command usage. Version prints the Simple tool version and whether LLVM ORC JIT support is compiled in. CLI `run` uses JIT by default when available. `-int` / `--interpreter` forces interpreter execution. `-jit` remains accepted for compatibility. `--jit-stats` prints JIT/fallback counters.

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

Runs the selected input through the necessary pipeline and executes it in the VM. JIT is enabled by default when available; pass `-int` or `--interpreter` to force interpreter execution.

### `check`

Validates syntax/semantics or bytecode structure without execution. For `.simple`, this means language validation; for `.sbc`, loader/verifier checks apply.

### `emit`

Emits intermediate output. Current workflows use it primarily for SIR/SBC inspection.

### `build` / `compile`

Builds an output artifact. `svm` produces executable stubs unless the output path ends in `.sbc`, in which case bytecode is written. `simple` does not build or compile.

### `lsp`

Starts the JSON-RPC language server over stdio. This is an `svm` command; `simple` does not start LSP.

## Diagnostics

CLI diagnostics use stable `error[Exxxx]:` prefixes where command-level errors apply. Compiler diagnostics are produced by the language pipeline and rendered for terminal output.

Examples of command-level failures include missing input, unsupported extension for the selected compatibility mode, bad arguments, load/verify failure, and runtime failure.

## Imports in CLI workflows

When compiling `.simple`, the CLI resolves local imports, project-root imports, module-map entries, and reserved standard-library imports. Generated `simple.modules` files are build artifacts and should not be committed.

## Build scripts

Use the root scripts for local builds:

```bash
./build.sh
```

```bat
build.bat
```

They build the CMake `simplevm` compiler target and `simple_stub` runtime target, then publish only `bin/svm` plus `bin/simple` at the repository root. The maintained CMake build tree is `Compiler/build`; stale alternate build trees such as `build-llvm` or `build-lsp` should not be kept.
