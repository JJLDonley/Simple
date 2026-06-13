# Simple TODO

This list tracks work needed to improve feature independence, compiler structure, runtime safety, and tooling. It intentionally excludes `i128`/`u128` implementation work.

## High Priority: Native SVM Standard Library

All Simple standard/core library modules should be implemented as native C++ runtime functions integrated into SVM. They should not be implemented as DL libraries.

- [ ] Add `Thread` core module:
  - [x] `Thread.sleep(ms)`
  - [x] `Thread.yield()`
  - [x] `Thread.hardwareConcurrency()`
  - [ ] VM thread/job handle type
  - [ ] `Thread.spawn(...)` for isolated VM jobs
  - [ ] `Thread.join(handle)`
  - [ ] `Thread.detach(handle)`
  - [ ] error propagation from worker jobs
  - [ ] shutdown/cancellation policy
- [ ] Add `Channel` core module for safe message passing between VM jobs:
  - [x] initial `Channel` native module with `newI32`, `sendI32`, `recvI32`, `tryRecvI32`, `close`
  - [x] remaining concrete primitive channels: `ChannelI64`, `ChannelF32`, `ChannelF64`, `ChannelBool`
  - [x] `ChannelString`
  - [x] `ChannelBytes`
  - [x] `send`, `trySend`, `recv`, `tryRecv`, `close`
  - [ ] non-blocking receive pattern for game loops
  - [x] clear rules: channels copy values; no shared mutable Simple heap in first version
  - [ ] later generic `Channel<T>` once runtime/type support is ready
- [ ] Expand native stdlib modules:
  - [x] `Path`: join, dirname, basename, ext, normalize, exists, isFile, isDir
  - [ ] `FS`:
    - [x] read/write text
    - [x] read/write bytes
    - [x] copy, remove, mkdir, mkdirAll, cwd, setCwd
    - [x] listDir
  - [x] `Env`: args, get, set, platform, arch, exePath
  - [x] `Time`: monotonic clocks, wall clocks, formatting helpers
  - [x] `Random`: seed, `i32`, integer ranges, `f64`
  - [x] `Log`:
    - [x] levels
    - [x] stdout/stderr sinks
    - [x] file sink
  - [x] `Bytes`/`Buffer`: endian-safe binary reads/writes, slice/copy helpers
  - [ ] `Json`: parse/stringify with handle-based API initially
  - [ ] `Net`: TCP/UDP sockets suitable for servers and games
  - [ ] `Http`: simple GET/POST convenience API
- [ ] Define stdlib native binding architecture:
  - [ ] registration table for native modules/functions
  - [ ] type-safe argument/result marshalling from VM values
  - [ ] shared diagnostics for invalid native calls
  - [ ] documentation generation from native binding metadata
  - [ ] tests for each stdlib function on all supported OSes

## Low Priority: Packaging / Distribution

- [ ] Add `svm package` after core runtime/library work stabilizes.
- [ ] Current-platform package first; multi-target package assembly later.
- [ ] Package embedded-SBC stubs, assets, and developer-provided dynamic libraries without trying to manage application-specific DL policy.

## Lang Architecture

- [ ] Replace facade-only Lang split with real implementation modules:
  - [ ] `Lexer`
  - [ ] `CAST`
  - [ ] `AST`
  - [ ] `RAST`
  - [ ] `TAST`
  - [ ] `IRB`
  - [ ] `IRE`
- [x] Move lexer implementation into `Lang/src/Lexer`.
- [x] Move parser implementation into `Lang/src/CAST`.
- [ ] Add real `CAST -> AST` normalization pass.
  - [x] Add initial `CAST -> AST` lowering boundary.
- [ ] Extract name/import/symbol resolution into `RAST`.
  - [x] Add initial RAST symbol collection boundary.
  - [x] Add callable parameter/local scope symbol collection.
- [ ] Extract type checking into `TAST`.
  - [x] Add initial TAST type-checker boundary.
- [ ] Replace direct SIR string emission with `IRB -> IRE`.
  - [x] Add initial `TAST -> IRB -> IRE` bridge.
- [ ] Update `CMakeLists.txt` as source files move into phase directories.
- [ ] Define deprecation/migration policy for legacy includes:
  - [ ] `lang_ast.h`
  - [ ] `lang_parser.h`
  - [ ] `lang_validate.h`
  - [ ] `lang_sir.h`

## Validator / Semantic Analysis

- [ ] Split `lang_validate.cpp` into focused modules:
  - [ ] resolver
  - [ ] type checker
  - [ ] control-flow checker
  - [ ] mutability checker
  - [ ] ABI checker
  - [ ] generics checker/substitution
- [ ] Add a canonical semantic context object carrying:
  - [ ] current function
  - [ ] current artifact
  - [ ] expected return type
  - [ ] expected expression type
  - [ ] loop depth
  - [ ] scope stack
- [ ] Replace repeated string-based lookup with resolved IDs:
  - [ ] `SymbolId`
  - [ ] `TypeId`
  - [ ] `FunctionId`
  - [ ] `ArtifactId`
  - [ ] `ModuleId`
  - [ ] `FieldId`
  - [ ] `EnumId`
  - [ ] `ExternId`
- [ ] Move contextual literal typing into TAST.
  - [x] Add initial TAST literal typing helpers.
- [ ] Persist expression types, mutability facts, and ABI facts on typed nodes.
- [x] Create canonical control-flow result:
  - [x] `may_fallthrough`
  - [x] `always_returns`
  - [x] `may_break`
  - [x] `may_skip`
- [x] Use the canonical control-flow result in both validation and lowering.

## AST / Feature Independence

- [x] Normalize top-level script body into an explicit AST node.
- [x] Normalize function literal declaration forms.
- [x] Normalize loop shorthand.
- [x] Normalize `|>` if-chain or define why it remains distinct.
- [x] Normalize switch branch forms.
- [x] Normalize call/member/index shapes.
- [x] Separate switch branch result markers from normal function `return` semantics.
- [ ] Formalize switch semantics:
  - [x] statement switch
  - [x] expression switch
  - [x] assigning switch
  - [x] block branch result rules
  - [x] early function return inside branch blocks
  - [x] `break`/`skip` behavior when nested in loops
  - [x] no-fallthrough policy
- [x] Add resolved receiver model for artifact methods.
  - [x] Add initial RAST artifact member receiver refs from local variable types.
  - [x] Persist artifact receiver type and symbol on RAST member refs.
- [x] Disambiguate member access during resolution:
  - [x] Add initial RAST member reference collection for static and `self` member refs.
  - [x] module member
  - [x] artifact field
  - [x] artifact method
  - [x] enum member
  - [x] reserved module function
  - [x] extern symbol
  - [x] DL manifest call

## Procedure Values / Generics / Pointers

- [x] Harden procedure values with tests across:
  - [x] artifact methods
  - [x] module functions
  - [x] switch expressions
  - [x] nested closures
  - [x] lists/arrays
  - [x] extern boundaries
  - [x] generics
  - [x] member calls
- [x] Audit closure/upvalue semantics.
- [x] Finish or explicitly reject unsupported generic cases:
  - [x] generic functions
  - [x] generic artifacts
  - [x] generic methods
  - [x] type-argument inference
  - [x] specialization naming/mangling
  - [x] duplicate specialization handling
- [x] Finish or explicitly reject unsupported pointer cases:
  - [x] pointer storage model
  - [x] pointer mutability
  - [x] pointer to artifact/list/array/string
  - [x] dereference rules
  - [x] pointer assignment
  - [x] pointer ABI behavior
  - [x] null pointer semantics
  - [x] pointer safety diagnostics

## IR / SIR / Lowering

- [x] Introduce structured language IR:
  - [x] `IrModule`
  - [x] `IrFunction`
  - [x] `IrBlock`
  - [x] `IrInst`
  - [x] `IrType`
  - [x] `IrSig`
  - [x] `IrImport`
- [x] Move artifact layout out of SIR emitter.
- [x] Move ABI flattening out of SIR emitter.
- [x] Move stack tracking into IRB.
- [x] Move local/global/import/signature allocation into IRB.
- [x] Make IRE serialize already-computed IR only.
- [x] Keep SIR output stable while replacing internals.
- [x] Add typed metadata builders instead of raw section byte buffers.

## Bytecode / Verifier / VM Coupling

- [x] Centralize opcode semantics:
  - [x] operand width
  - [x] stack pops/pushes
  - [x] type rules
  - [x] control-flow behavior
  - [x] verifier rule
  - [x] VM dispatch mapping
- [x] Add tests comparing opcode metadata, verifier behavior, and VM stack behavior.
- [x] Reduce verifier/VM opcode semantic drift.
- [ ] Freeze or version opcode metadata used by external SBC producers.

## VM / Runtime

- [ ] Add typed heap layout helpers for:
  - [ ] string
  - [ ] array
  - [ ] list
  - [ ] artifact
  - [ ] closure
- [ ] Harden GC root tracing with stress tests for:
  - [ ] nested lists of strings
  - [ ] arrays of artifacts
  - [ ] closures capturing refs
  - [ ] artifact fields containing refs
  - [ ] switch/loop local lifetimes with refs
  - [ ] globals holding refs
  - [ ] temporary stack refs during calls
- [ ] Define and enforce runtime limits:
  - [ ] max stack
  - [ ] max locals
  - [ ] max call depth
  - [ ] max heap objects/bytes
  - [ ] max array/list size
  - [ ] max const pool size
  - [ ] max code size
- [ ] Keep interpreter as canonical until JIT behavior is fully specified.

## DL / FFI

- [ ] Write exact ABI mapping table.
- [ ] Document artifact layout rules.
- [ ] Document enum representation.
- [ ] Document string ownership/lifetime rules.
- [ ] Document pointer ownership rules.
- [ ] Add unsupported-shape diagnostics.
- [ ] Document and test Windows behavior or explicit lack of support.
- [ ] Move ABI checking/lowering into dedicated passes.

## CLI / Diagnostics / Imports

- [ ] Freeze CLI exit-code contract.
- [ ] Freeze stderr diagnostic format.
- [ ] Define `svm`, `simple`, and `simplevm` behavior precisely.
- [ ] Document build output behavior.
- [ ] Document executable embedding/linking behavior.
- [ ] Document dynamic/static flags.
- [ ] Document missing file/import errors.
- [ ] Add stable diagnostic code ranges:
  - [ ] lexer: `E1xxx`
  - [ ] parser: `E2xxx`
  - [ ] resolver: `E3xxx`
  - [ ] type checker: `E4xxx`
  - [ ] IR/lowering: `E5xxx`
  - [ ] bytecode/verifier: `E6xxx`
  - [ ] runtime: `E7xxx`
  - [ ] CLI/imports: `E8xxx`
- [ ] Move import graph construction out of CLI into Lang/RAST or a shared module.
- [ ] Make CLI, LSP, and tests use the same import graph logic.

## Parser / Error Recovery / LSP

- [ ] Add systematic parser recovery after bad declarations.
- [ ] Improve parser recovery inside blocks.
- [ ] Support multiple diagnostics per file.
- [ ] Produce LSP-friendly partial ASTs.
- [ ] Keep stable spans for incomplete code.
- [ ] Integrate LSP with compiler phases:
  - [ ] CAST parse diagnostics
  - [ ] TAST semantic diagnostics
  - [ ] RAST go-to-definition
  - [ ] scope-based completion
  - [ ] resolved semantic tokens
  - [ ] symbol-ID rename

## Tests

- [ ] Split large test files by compiler phase:
  - [ ] `test_lexer.cpp`
  - [ ] `test_cast.cpp`
  - [ ] `test_ast.cpp`
  - [ ] `test_rast.cpp`
  - [ ] `test_tast.cpp`
  - [ ] `test_irb.cpp`
  - [ ] `test_ire.cpp`
  - [ ] `test_lang_integration.cpp`
- [ ] Add combinatorial feature-independence matrix tests:
  - [ ] artifact method -> switch -> if-chain -> loop -> list
  - [ ] module function -> fn literal -> switch -> artifact init
  - [ ] generic method -> array/list -> member call
  - [ ] extern call -> artifact literal -> nested artifact
- [ ] Add positive and negative tests for each nested feature combination.
- [ ] Add fixture tests for all unsupported-but-diagnosed feature combinations.

## Docs / Compatibility

- [ ] Add per-feature implementation status table:
  - [ ] parse
  - [ ] validate
  - [ ] emit
  - [ ] verify
  - [ ] runtime
  - [ ] tests
  - [ ] notes
- [ ] Freeze or explicitly version:
  - [ ] Lang syntax version
  - [ ] SIR version
  - [ ] SBC version
  - [ ] runtime ABI version
  - [ ] stdlib module version
- [ ] Decide whether `Docs/Sprint.md` keeps historical obsolete commands as-is or gains an obsolete-command note.
