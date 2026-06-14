# Simple Implementation Status and Roadmap

This document aligns the current compiler, language, bytecode, and VM documentation with the implementation in this repository.

For detailed module contracts, see:

- `Docs/Language.md`
- `Docs/IR.md`
- `Docs/Byte.md`
- `Docs/VM.md`
- `Docs/JIT.md`
- `Docs/CLI.md`
- `Docs/StdLib.md`
- `Docs/LSP.md`

## Current Pipeline

Implemented end-to-end pipeline:

```txt
.simple source
  -> Lang lexer/parser
  -> AST
  -> Lang validator
  -> SIR text emission
  -> IR text parser/lowerer
  -> SBC binary emission
  -> Byte loader
  -> Byte verifier
  -> VM interpreter
```

The interpreter is the canonical runtime. JIT-related structures exist but are not the correctness baseline.

## Implemented

### Repository Modules

Implemented module layout:

- `Lang/` - lexer, parser, AST, validator, SIR emitter
- `IR/` - SIR parser/lowerer, bytecode builder, SBC compiler
- `Byte/` - SBC format, opcode table, loader, verifier
- `VM/` - interpreter, heap, runtime imports, dynamic library calls, JIT scaffolding
- `CLI/` - command-line orchestration and diagnostics
- `LSP/` - language server
- `Editor/` - VS Code extension baseline
- `Tests/` - C++ tests and `.simple` fixtures
- `Docs/` - current and legacy documentation

### Language Front-End

Implemented:

- tokenization with comments, literals, keywords, operators, and source locations
- recursive-descent parser
- AST for declarations, statements, expressions, types, modules, artifacts, enums, externs, and imports
- strict semantic validation
- mutability checks
- function and variable declarations
- top-level script statements through implicit script entry
- explicit `main` entry when no top-level script body is present
- primitive scalar types
- arrays and lists
- artifacts with fields/methods
- modules with variables/functions
- scoped enums
- procedure types and function literals for supported paths
- control flow: `if`, chain `|>`, `while`, C-style `for`, `break`, `skip`, `return`
- switch-expression parsing/validation for supported value forms
- casts with `@T(expr)`
- format strings
- reserved imports
- local file imports through CLI orchestration
- extern declarations and DL ABI validation
- SIR text emission

### Compiler / IR

Implemented:

- SIR text sections for types, signatures, constants, imports, globals, functions, and entry
- SIR text parser
- SIR lowerer
- label resolution and branch fixups
- jump-table fixups
- opcode mnemonic lowering
- local/global/upvalue name mapping
- type/signature metadata generation
- import metadata generation
- global metadata generation
- SBC binary packing

### Bytecode

Implemented:

- SBC header and section-table format
- metadata tables for types, fields, methods, signatures, globals, functions, imports, exports, constants, code, and debug bytes
- opcode enum and opcode metadata
- loader structural validation
- verifier control-flow validation
- verifier stack/type/call validation
- intrinsic/syscall validation
- table and const-pool checks

### VM / Runtime

Implemented:

- 64-bit slot interpreter
- call frames
- direct calls
- indirect calls where verified/emitted
- tail calls
- locals/globals/upvalues
- arithmetic/comparison/boolean/conversion opcodes
- arrays
- lists
- strings
- artifacts/objects
- closures for supported bytecode forms
- heap allocation
- mark/sweep heap operations
- core runtime import dispatch
- intrinsic dispatch
- dynamic library calls through libffi on supported platforms
- non-recursive artifact by-value ABI marshalling
- nested artifact ABI flattening
- runtime traps with `ExecResult`
- JIT counters/tier scaffolding with interpreter fallback posture

### CLI

Implemented command surface:

```txt
simplevm run <module.sbc|file.sir|file.simple> [--no-verify]
simplevm build <file.sir|file.simple> [--out <file.sbc>] [--no-verify]
simplevm compile <file.sir|file.simple> [--out <file.sbc>] [--no-verify]
simplevm emit -ir <file.simple> [--out <file.sir>]
simplevm emit -sbc <file.sir|file.simple> [--out <file.sbc>] [--no-verify]
simplevm check <file.sbc|file.sir|file.simple>
simplevm lsp
```

When installed or invoked as `simple`, the CLI narrows user-facing workflows to `.simple` inputs and may build an executable by default depending on output mode.

Implemented CLI support:

- source diagnostics with line/caret context
- `.simple` import graph loading
- reserved import preservation
- relative/absolute import resolution
- project-root bare-file lookup
- cycle detection
- ambiguity diagnostics
- SIR/SBC emission
- check/run/build flows
- LSP server launch

### LSP / Editor

Implemented according to current docs/tests:

- LSP server command through `simple lsp` / `simplevm lsp`
- diagnostics
- completion/navigation/highlighting-related protocol support
- VS Code extension baseline

### Tests

Implemented test coverage includes:

- core bytecode/runtime tests
- IR tests
- JIT/scaffolding tests
- language positive and negative fixtures
- LSP tests
- FFI fixtures

Current build entrypoints are platform scripts:

```txt
./build_linux
./build_macos
./build_windows
```

`./build_linux --suite ...` is not the current script interface.

## In Progress

These areas exist but should be considered under active hardening or not fully frozen as stable contracts.

### Language

- full generic language support and complete monomorphization behavior
- complete first-class procedure value coverage across all runtime paths
- complete pointer semantics beyond currently parsed/validated/emitted forms
- complete switch-expression matrix across all branch/block/value combinations
- broader diagnostics with stable error codes
- exhaustive documentation for every accepted/rejected syntax edge case

### Compiler / IR

- formal SIR grammar and compatibility policy
- stable external SIR contract
- structured SIR builder to reduce string-emission coupling
- typed metadata builders instead of raw section byte buffers
- complete debug/export section semantics
- archived SIR fixtures

### Bytecode

- formal SBC versioning and compatibility policy
- archived SBC compatibility fixtures
- complete opcode semantic metadata shared by compiler, verifier, and VM
- debug-section format freeze
- export-section format freeze
- metadata flag registry

### VM

- mature JIT coverage and tier behavior
- explicit JIT eligibility rules
- broader closure/upvalue stress coverage
- stronger GC/root stress coverage
- formal stack/heap/frame limits
- cross-platform DL parity
- stable embedding/runtime ABI beyond current C++ API

### CLI / Tooling

- frozen exit-code contract for all commands
- stable machine-readable diagnostic format
- command docs synchronized with installer/release behavior
- CI matrix and release-gate documentation
- smoke profile that covers source -> SIR -> SBC -> VM quickly

## Future

Not implemented as stable current contract.

### Language Future

- package manager and package-aware imports
- traits/interfaces/advanced generic constraints
- macro system
- async/concurrency language features
- borrow/lifetime system
- richer pattern matching
- advanced module visibility/package privacy

### Compiler Future

- optimizing middle-end
- SSA IR
- incremental compilation
- native/AOT backend
- plugin pass API
- source-map/debug-info format freeze

### Bytecode Future

- version negotiation
- forward/backward compatibility guarantees across released SBC versions
- compressed sections
- signed bytecode artifacts
- standalone bytecode optimizer

### VM Future

- full optimizing JIT
- generational/incremental GC
- sandboxing for untrusted bytecode
- stable debugger/profiler protocol
- multithreaded runtime
- Windows `System.dl` parity

### Tooling Future

- package manager commands
- formatter
- richer linter
- language server workspace indexing beyond current scope
- release/install command contract freeze

## Alpha Readiness Checklist

Before declaring a stable alpha contract, the following should be true:

1. `Docs/Language.md`, `Docs/IR.md`, `Docs/Byte.md`, `Docs/VM.md`, `Docs/JIT.md`, and `Docs/CLI.md` match tested behavior.
2. Build/test commands in docs match actual scripts.
3. SBC compatibility/versioning policy is explicit.
4. SIR accepted syntax is explicitly documented.
5. CLI exit codes and diagnostic formats are documented.
6. JIT support posture is explicit: interpreter is canonical.
7. Release artifacts and installer behavior are tested from a clean checkout.
8. Archived SIR/SBC fixtures exist for compatibility smoke tests.

## Current Recommended Verification Commands

From this repository, use the platform build scripts. Examples:

```sh
./build_linux
./build_linux --tests
./build_linux --no-tests
```

Then run generated test binaries from the configured CMake build output if needed. The exact binary path depends on the script/options and platform build directory.
