# Simple TODO

This list tracks work needed to improve feature independence, compiler structure, runtime safety, and tooling. It intentionally excludes `i128`/`u128` implementation work.

## Highest Priority: SRP / Module Boundary Refactors

These refactors must happen before large new feature work. The current risk is that the project is feature-rich, but several central files absorb too much behavior. `standards.md` is the mandatory coding standard for this work.

Primary monoliths/offenders:

1. `VM/src/vm.cpp` — split first.
2. `Lang/src/lang_validate.cpp` — split second.
3. `Tests/tests/test_lang.cpp` — split after phase boundaries stabilize.
4. `Tests/tests/test_System.cpp` — split by VM subsystem.
5. `CLI/src/main.cpp` — split diagnostics/import/build helpers after shared import graph extraction.

Recommended priority order:

1. Split VM native/runtime boundaries.
2. Split language validation boundaries.
3. Split tests by subsystem/phase.
4. Split CLI/import/diagnostic services.
5. Then add large features like Thread jobs, Net, and Http.

Reason: Thread/Net/Http will be painful and high-risk if added into the current monoliths.

End-state rule: this refactor must not leave permanent shims, compatibility facades, facade-only modules, or forwarding wrappers. Temporary facades are allowed only inside an active migration step and must be removed before the refactor is considered complete.

### SRP Phase 1: VM Native/Runtime Extraction

- [ ] Split `VM/src/vm.cpp` into explicit modules:
  - [x] `VM/src/interpreter/interpreter.cpp`
  - [x] `VM/src/interpreter/dispatch.cpp`
  - [x] `VM/src/interpreter/frames.cpp`
  - [x] `VM/src/interpreter/stack.cpp`
  - [x] `VM/src/native/registry.cpp`
  - [x] `VM/src/native/os.cpp`
  - [x] `VM/src/native/fs.cpp`
  - [x] `VM/src/native/path.cpp`
  - [x] `VM/src/native/env.cpp`
  - [x] `VM/src/native/time.cpp`
  - [x] `VM/src/native/random.cpp`
  - [x] `VM/src/native/log.cpp`
  - [x] `VM/src/native/channel.cpp`
  - [x] `VM/src/native/buffer.cpp`
  - [x] `VM/src/native/json.cpp`
  - [x] `VM/src/native/thread.cpp`
  - [x] `VM/src/ffi/dl_runtime.cpp`
  - [x] `VM/src/jit/jit_scaffold.cpp`
  - [x] `VM/src/gc/root_tracer.cpp`
  - [x] `VM/src/runtime/runtime_limits.cpp`
- [ ] Define explicit VM boundary types:
  - [x] `NativeCallContext`
  - [x] `NativeCallResult`
  - [x] `NativeModule`
  - [x] `NativeFunction`
  - [x] `FrameState`
  - [x] `InterpreterState`
  - [x] `RootTraceContext`
- [ ] Replace real subsystem lambdas in `vm.cpp` with named functions/types.
- [ ] Interpreter module owns only opcode loop, stack operations, frames, locals/globals, calls/tailcalls, and traps.
- [ ] Interpreter module must not own native stdlib implementation, DL/FFI internals, JSON parser, channel registries, or platform FS code.

### SRP Phase 2: Native Binding Metadata

- [x] Add `VM/include/native/registry.h`.
- [x] Add `VM/src/native/registry.cpp`.
- [x] Define `NativeFunctionSpec` metadata:
  - [x] module name
  - [x] symbol name
  - [x] parameter types
  - [x] result type
  - [x] handler function
- [ ] Use native metadata for VM runtime dispatch.
  - [x] Route `System.random` through native metadata dispatch.
  - [x] Route `System.os` time/sleep plus cwd/format helpers through native metadata dispatch.
  - [x] Route `System.thread` through native metadata dispatch.
  - [x] Route `System.channel` scalar plus string/bytes new/pending helpers through native metadata dispatch.
  - [x] Route `System.json.free` and `System.log.setLevel` through native metadata dispatch.
  - [x] Route `System.buffer.new`, len, LE read/write, slice, and copy helpers through native metadata dispatch.
  - [x] Route `System.env` platform/arch/exePath helpers through native metadata dispatch.
  - [x] Route `System.path` helpers through native metadata dispatch.
- [ ] Use native metadata for Lang reserved module signature generation.
- [ ] Use native metadata for stdlib documentation generation.
- [ ] Remove native stdlib forwarding glue once metadata dispatch is complete.
- [ ] Native functions must use named handlers such as `FsReadText`, `ChannelPendingI32`, and `JsonParse`.

### SRP Phase 3: Language Validation Split

- [ ] Split `Lang/src/lang_validate.cpp` into RAST modules:
  - [ ] `Lang/src/RAST/import_graph.cpp`
  - [ ] `Lang/src/RAST/symbol_table.cpp`
  - [ ] `Lang/src/RAST/resolver.cpp`
  - [ ] `Lang/src/RAST/member_resolution.cpp`
  - [ ] `Lang/src/RAST/reserved_resolution.cpp`
- [ ] RAST owns names and symbols only:
  - [ ] symbol lookup
  - [ ] import resolution
  - [ ] member resolution by name
  - [ ] declaration reference resolution
- [ ] RAST must not decide arithmetic type validity, all-paths-return, or literal contextual typing.
- [ ] Split `Lang/src/lang_validate.cpp` into TAST modules:
  - [ ] `Lang/src/TAST/type_checker.cpp`
  - [ ] `Lang/src/TAST/expressions.cpp`
  - [ ] `Lang/src/TAST/statements.cpp`
  - [ ] `Lang/src/TAST/calls.cpp`
  - [ ] `Lang/src/TAST/literals.cpp`
  - [ ] `Lang/src/TAST/mutability.cpp`
  - [ ] `Lang/src/TAST/control_flow.cpp`
  - [ ] `Lang/src/TAST/generics.cpp`
  - [ ] `Lang/src/TAST/abi.cpp`
- [ ] TAST owns type facts and produces/persists:
  - [ ] `TypedProgram`
  - [ ] `TypedExpr`
  - [ ] `TypedStmt`
  - [ ] `ExprTypeMap`
  - [ ] `MutabilityFacts`
  - [ ] `AbiFacts`
- [ ] Use phase-specific function names such as `ResolveProgram`, `ResolveMemberAccess`, `CheckCallExpression`, `CheckAssignment`, `CheckReturnFlow`, `CheckAbiShape`, and `SubstituteGenericTypes`.
- [ ] Avoid generic multi-purpose names like `ValidateThing` or broad `InferExprType` helpers that hide multiple responsibilities.

### SRP Phase 4: Structured Diagnostics

- [ ] Add `Lang/include/Diagnostics/diagnostic.h`.
- [ ] Add `Lang/src/Diagnostics/diagnostic.cpp`.
- [ ] Add `CLI/src/diagnostic_render.cpp`.
- [ ] Add `LSP/src/diagnostic_bridge.cpp`.
- [ ] Define structured diagnostics with:
  - [ ] diagnostic code
  - [ ] source span
  - [ ] phase
  - [ ] message
  - [ ] help text
- [ ] Compiler phases should return/report diagnostics instead of only plain strings.
- [ ] CLI should render diagnostics only; it should not infer diagnostic codes from string matching.
- [ ] LSP should consume the same structured diagnostics as CLI.

### SRP Phase 5: Test Split

- [ ] Split VM tests out of `Tests/tests/test_System.cpp`:
  - [ ] `Tests/tests/vm/test_interpreter.cpp`
  - [ ] `Tests/tests/vm/test_heap.cpp`
  - [ ] `Tests/tests/vm/test_gc.cpp`
  - [ ] `Tests/tests/vm/test_runtime_limits.cpp`
  - [ ] `Tests/tests/vm/test_native_fs.cpp`
  - [ ] `Tests/tests/vm/test_native_channel.cpp`
  - [ ] `Tests/tests/vm/test_jit.cpp`
- [ ] Split language tests out of `Tests/tests/test_lang.cpp`:
  - [ ] `Tests/tests/lang/test_lexer.cpp`
  - [ ] `Tests/tests/lang/test_cast.cpp`
  - [ ] `Tests/tests/lang/test_ast.cpp`
  - [ ] `Tests/tests/lang/test_rast.cpp`
  - [ ] `Tests/tests/lang/test_tast.cpp`
  - [ ] `Tests/tests/lang/test_irb.cpp`
  - [ ] `Tests/tests/lang/test_ire.cpp`
  - [ ] `Tests/tests/lang/test_integration.cpp`
- [ ] Split CLI tests out of `Tests/tests/test_lang.cpp`:
  - [ ] `Tests/tests/cli/test_cli_contract.cpp`
  - [ ] `Tests/tests/cli/test_cli_diagnostics.cpp`
  - [ ] `Tests/tests/cli/test_cli_build.cpp`
  - [ ] `Tests/tests/cli/test_cli_imports.cpp`
- [ ] Migration order:
  - [ ] heap/gc tests first
  - [ ] lexer/parser tests second
  - [ ] CLI tests third
  - [ ] RAST/TAST tests fourth

### SRP Phase 6: CLI / Import / Build Service Split

- [ ] Move import graph construction out of CLI into shared Lang/RAST service.
- [ ] Make CLI, LSP, and tests use the same import graph implementation.
- [ ] Move CLI diagnostic rendering into `CLI/src/diagnostic_render.cpp`.
- [ ] Move CLI build/embed/link helpers out of `CLI/src/main.cpp`.
- [ ] Move CLI command parsing/dispatch into dedicated command modules.
- [ ] Remove CLI/LSP/test duplicate import wrappers after shared import graph adoption.

### SRP Phase 7: Documentation / Ownership

- [ ] Add/update architecture ownership docs:
  - [ ] `Docs/Architecture.md`
  - [ ] `Docs/LanguagePipeline.md`
  - [ ] `Docs/NativeBindings.md`
  - [ ] `Docs/Diagnostics.md`
- [ ] Each subsystem doc must list:
  - [ ] owned files
  - [ ] forbidden dependencies
  - [ ] public API
  - [ ] tests
- [ ] Keep `Docs/TODO.md` actionable with file-level tasks, not broad statements.

### SRP Phase 8: No-Shim / No-Facade End State

- [ ] Remove legacy `lang_*.h` compatibility facades after callers move to phase headers:
  - [ ] remove `lang_ast.h`
  - [ ] remove `lang_parser.h`
  - [ ] remove `lang_validate.h`
  - [ ] remove `lang_sir.h`
- [ ] Remove facade-only Lang phase modules; each phase must own real implementation.
- [ ] Remove native stdlib forwarding glue once binding metadata dispatch is in place.
- [ ] Remove CLI/LSP/test duplicate import wrappers once shared import graph is adopted.
- [ ] Add tests/checks that fail if removed legacy headers/facades are reintroduced.

### SRP Code Review Checks

- [ ] VM review gate: adding code to `VM/src/vm.cpp` requires written justification.
- [ ] Language review gate: adding code to `Lang/src/lang_validate.cpp` requires written justification.
- [ ] Native runtime review gate: native code must have metadata/signature tests.
- [ ] GC review gate: GC code must use layout/ref maps rather than guesses.
- [ ] Language phase review gate: new semantic logic must identify whether it belongs to RAST or TAST.
- [ ] Test review gate: new tests must live in the correct subsystem after test split.

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
  - [x] non-blocking receive pattern for game loops
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
  - [x] `Json`: parse/stringify with handle-based API initially
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
- [ ] Remove legacy include facades after migration:
  - [ ] remove `lang_ast.h`
  - [ ] remove `lang_parser.h`
  - [ ] remove `lang_validate.h`
  - [ ] remove `lang_sir.h`
  - [ ] update all project includes to phase headers
  - [ ] delete compatibility tests that only exist to preserve legacy facades

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
- [x] Freeze or version opcode metadata used by external SBC producers.

## VM / Runtime

- [x] Add typed heap layout helpers for:
  - [x] string
  - [x] array
  - [x] list
  - [x] artifact
  - [x] closure
- [ ] Harden GC root tracing with stress tests for:
  - [x] nested lists of strings
  - [x] arrays of artifacts
  - [x] closures capturing refs
  - [x] artifact fields containing refs
  - [ ] switch/loop local lifetimes with refs
  - [ ] globals holding refs
  - [ ] temporary stack refs during calls
- [x] Define and enforce runtime limits:
  - [x] max stack
  - [x] max locals
  - [x] max call depth
  - [x] max heap objects/bytes
  - [x] max array/list size
  - [x] max const pool size
  - [x] max code size
- [x] Keep interpreter as canonical until JIT behavior is fully specified.

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

- [x] Freeze CLI exit-code contract.
- [x] Freeze stderr diagnostic format.
- [x] Define `svm`, `simple`, and `simplevm` behavior precisely.
- [x] Document build output behavior.
- [ ] Document executable embedding/linking behavior.
- [x] Document dynamic/static flags.
- [x] Document missing file/import errors.
- [x] Add stable diagnostic code ranges:
  - [x] lexer: `E1xxx`
  - [x] parser: `E2xxx`
  - [x] resolver: `E3xxx`
  - [x] type checker: `E4xxx`
  - [x] IR/lowering: `E5xxx`
  - [x] bytecode/verifier: `E6xxx`
  - [x] runtime: `E7xxx`
  - [x] CLI/imports: `E8xxx`
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
- [x] Freeze or explicitly version:
  - [x] Lang syntax version
  - [x] SIR version
  - [x] SBC version
  - [x] runtime ABI version
  - [x] stdlib module version
- [ ] Decide whether `Docs/Sprint.md` keeps historical obsolete commands as-is or gains an obsolete-command note.
