# Simple::CLI (Current Contract)

This document describes the command-line interface implemented in `CLI/src/main.cpp`.

## Implemented

### Binaries

The code supports two invocation modes by executable name:

- `simple` - primary user-facing name, focused on `.simple` workflows
- `simplevm` - compatibility/developer tool name, accepts `.simple`, `.sir`, and `.sbc` paths where applicable

### Version and Help

Implemented:

```txt
--version
-v
version
--help
-h
help
```

### Commands

Implemented commands:

```txt
run
check
build
compile
emit
lsp
```

`compile` is an alias of `build`.

### `simplevm` Usage

Implemented usage shape:

```txt
simplevm run <module.sbc|file.sir|file.simple> [--no-verify]
simplevm build <file.sir|file.simple> [--out <file.sbc>] [--no-verify]
simplevm compile <file.sir|file.simple> [--out <file.sbc>] [--no-verify]
simplevm emit -ir <file.simple> [--out <file.sir>]
simplevm emit -sbc <file.sir|file.simple> [--out <file.sbc>] [--no-verify]
simplevm check <file.sbc|file.sir|file.simple>
simplevm lsp
simplevm <module.sbc|file.sir|file.simple> [--no-verify]
```

### `simple` Usage

Implemented user-facing usage shape:

```txt
simple run <file.simple> [--no-verify]
simple build <file.simple> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]
simple compile <file.simple> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]
simple emit -ir <file.simple> [--out <file.sir>]
simple emit -sbc <file.simple> [--out <file.sbc>] [--no-verify]
simple check <file.simple>
simple lsp
simple <file.simple> [--no-verify]
```

### Input Modes

Implemented by command/tool mode:

- `.simple` source files
- `.sir` IR text files for `simplevm` developer workflows
- `.sbc` bytecode modules for `simplevm` developer workflows

### Verification

`--no-verify` disables explicit verification in relevant run/build/emit paths. Verification is enabled by default where applicable.

### Emit

Implemented:

- `emit -ir <file.simple>` writes SIR text
- `emit -sbc <file.simple|file.sir>` writes SBC bytes
- `--out <path>` overrides output path

Default output extensions:

- `.simple -> .sir` for `emit -ir`
- `.simple/.sir -> .sbc` for `emit -sbc`

### Check

Implemented checks:

- `.simple`: parse/import/validate
- `.sir`: parse and lower SIR
- `.sbc`: load and verify SBC

### Build / Compile

Implemented:

- `.simple` or `.sir` to `.sbc`
- optional executable embedding path for `simple` mode depending on output/mode
- `--out <path>` output override
- `-d` / `--dynamic` executable build mode
- `-s` / `--static` executable build mode

### Run

Implemented run path:

```txt
.simple -> compile to SBC -> load -> verify unless disabled -> VM execute
.sir    -> compile to SBC -> load -> verify unless disabled -> VM execute
.sbc    -> load -> verify unless disabled -> VM execute
```

### LSP

Implemented:

```txt
simple lsp
simplevm lsp
```

This starts the language server over stdin/stdout.

### Import Resolution for `.simple`

Implemented CLI import loading:

- reserved imports remain as reserved stdlib/runtime imports
- non-reserved imports are resolved before validation/emission
- relative imports resolve from the importing file's directory
- absolute imports are accepted if present
- extensionless imports can append `.simple`
- bare names are looked up in the detected project root's `.simple` file index
- ambiguous bare-name imports are rejected
- cyclic imports are rejected

### Diagnostics

Implemented diagnostics behavior:

- failures return non-zero exit status
- many `.simple` diagnostics are printed as `error[E0001]: ...`
- source location and caret context are printed when line/column and source are available
- simple help hints are printed for selected common diagnostic classes

### Build Scripts

Implemented platform scripts in this repository:

```txt
./build_linux
./build_macos
./build_windows
```

Common options implemented by the scripts include:

- `--version <name>`
- `--prefix <dir>`
- `--bin-dir <dir>`
- `--tests`
- `--no-tests`
- `--legacy`
- `--skip-build`
- `--skip-release`
- `--skip-install`
- `--no-link`

The current scripts do not implement `--suite`.

## In Progress

- fully frozen exit-code contract for every command/failure class
- stable machine-readable diagnostic output
- complete documentation for executable embedding/linking behavior
- complete installer/release behavior documentation
- CI smoke profile documentation
- full parity between `simple` and `simplevm` docs after installation packaging is finalized

## Future

- package manager commands
- formatter command
- linter command
- test runner command
- machine-readable JSON diagnostics mode
- stable plugin/tooling interface
