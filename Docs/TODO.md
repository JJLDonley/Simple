# Simple TODO

This list tracks work needed to improve feature independence, compiler structure, runtime safety, and tooling. It intentionally excludes `i128`/`u128` implementation work.

## Highest Priority: SRP / Module Boundary Refactors

These refactors must happen before large new feature work. The current risk is that the project is feature-rich, but several central files absorb too much behavior. `Docs/Standards.md` is the mandatory coding standard for this work.

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

- [x] Split `VM/src/vm.cpp` into explicit modules:
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
- [x] Define explicit VM boundary types:
  - [x] `NativeCallContext`
  - [x] `NativeCallResult`
  - [x] `NativeModule`
  - [x] `NativeFunction`
  - [x] `FrameState`
  - [x] `InterpreterState`
  - [x] `RootTraceContext`
- [x] Replace and move real subsystem lambdas/helpers out of `vm.cpp` into owned modules.
  - [x] Move import-call dispatch into `VM/src/runtime/import_dispatch.cpp`.
  - [x] Move trap-formatting helpers into `VM/src/interpreter/traps.cpp`.
  - [x] Move JIT/trap operand-reader helpers into `VM/src/interpreter/traps.cpp`.
  - [x] Move JIT compiled failure formatting into `VM/src/jit/failure_format.cpp`.
  - [x] Move execution-result finalizer into `VM/src/runtime/execution_stats.cpp`.
  - [x] Move constant-string/global lookup helpers into `VM/src/interpreter/globals.cpp`.
  - [x] Move runtime-limit/local allocation helpers into runtime/interpreter modules.
  - [x] Move interpreter frame setup/local allocation helpers into `VM/src/interpreter/frames.cpp`.
  - [x] Move GC stack-map collection helpers into `VM/src/gc/stack_map_collection.cpp`.
  - [x] Move JIT tier-update helper into `VM/src/jit/tier_updater.cpp`.
  - [x] Move native metadata dispatch into `VM/src/native/dispatch.cpp`.
  - [x] Move heap payload/string/list helpers into `VM/src/heap.cpp`.
  - [x] Move string encoding helpers into `VM/src/heap.cpp`.
  - [x] Move const-pool string decoding into `Byte/src/sbc_loader.cpp`.
  - [x] Move slot/value packing helpers into `VM/include/runtime/values.h`.
  - [x] Move dynamic DL/FFI call machinery into `VM/src/ffi/dl_call.cpp`.
  - [x] Move JIT compile eligibility policy into `VM/src/jit/compile_policy.cpp`.
  - [x] Move JIT compiled runner into `VM/src/jit/compiled_runner.cpp`.
  - [x] Replace JIT compile predicate lambda with `Simple::VM::Jit::CompilePredicate`.
  - [x] Move `print_any` formatting into `VM/src/runtime/print_any.cpp`.
  - [x] Move JIT compiled runner into `VM/src/jit/compiled_runner.cpp`.
- [x] Interpreter module owns only opcode loop, stack operations, frames, locals/globals, calls/tailcalls, and traps.
- [x] Interpreter module must not own native stdlib implementation, DL/FFI internals, JSON parser, channel registries, or platform FS code.

### SRP Phase 2: Native Binding Metadata

- [x] Add `VM/include/native/registry.h`.
- [x] Add `VM/src/native/registry.cpp`.
- [x] Define `NativeFunctionSpec` metadata:
  - [x] module name
  - [x] symbol name
  - [x] parameter types
  - [x] result type
  - [x] handler function
- [x] Use native metadata for VM runtime dispatch.
  - [x] Route `System.random` through native metadata dispatch.
  - [x] Route `System.os` args/env/time/sleep plus cwd/format helpers through native metadata dispatch.
  - [x] Route `System.thread` through native metadata dispatch.
  - [x] Route `System.channel` scalar plus string/bytes new/send/recv/pending helpers through native metadata dispatch.
  - [x] Remove obsolete scalar channel forwarding glue after metadata dispatch.
  - [x] Remove hardcoded channel symbol dispatch list after metadata coverage.
  - [x] Route `System.json` and `System.log` helpers through native metadata dispatch.
  - [x] Route `System.buffer.new`, len, LE read/write, slice, and copy helpers through native metadata dispatch.
  - [x] Route `System.env` args/get/set/platform/arch/exePath helpers through native metadata dispatch.
  - [x] Route `System.path` helpers through native metadata dispatch.
  - [x] Route `System.fs` text/bytes/listDir/fd/cwd/copy/remove/mkdir/setCwd helpers through native metadata dispatch.
  - [x] Route `System.io` buffer helpers through native metadata dispatch.
  - [x] Route non-call `System.dl` helpers through native metadata dispatch.
- [x] Use native metadata for Lang reserved module signature generation.
  - [x] Use native metadata for RAST reserved module member recognition.
  - [x] Use native metadata as validation signature fallback for reserved native members.
  - [x] Use native metadata to emit fallback SIR imports for reserved native modules.
  - [x] Test metadata-provided reserved signatures and suggestions.
- [x] Use native metadata for stdlib documentation generation.
  - [x] Add native metadata Markdown generator for stdlib API snapshots.
  - [x] Test generated stdlib docs include every registered native function.
- [x] Remove native stdlib forwarding glue once metadata dispatch is complete.
- [x] Native functions must use named handlers such as `FsReadText`, `ChannelPendingI32`, and `JsonParse`.

### SRP Phase 3: Language Validation Split

- [x] Split `Lang/src/lang_validate.cpp` into RAST modules:
  - [x] `Lang/src/RAST/import_graph.cpp`
  - [x] `Lang/src/RAST/symbol_table.cpp`
  - [x] `Lang/src/RAST/resolver.cpp`
  - [x] `Lang/src/RAST/member_resolution.cpp`
  - [x] `Lang/src/RAST/reserved_resolution.cpp`
- [x] RAST owns names and symbols only:
  - [x] symbol lookup
  - [x] import resolution
  - [x] member resolution by name
  - [x] declaration reference resolution
- [x] RAST must not decide arithmetic type validity, all-paths-return, or literal contextual typing.
- [x] Split `Lang/src/lang_validate.cpp` into TAST modules:
  - [x] `Lang/src/TAST/type_checker.cpp`
  - [x] `Lang/src/TAST/expressions.cpp`
  - [x] `Lang/src/TAST/statements.cpp`
  - [x] `Lang/src/TAST/calls.cpp`
  - [x] `Lang/src/TAST/literals.cpp`
  - [x] `Lang/src/TAST/mutability.cpp`
  - [x] `Lang/src/TAST/control_flow.cpp`
  - [x] `Lang/src/TAST/generics.cpp`
  - [x] `Lang/src/TAST/abi.cpp`
- [x] TAST owns type facts and produces/persists:
  - [x] `TypedProgram`
  - [x] `TypedExpr`
  - [x] `TypedStmt`
  - [x] `ExprTypeMap`
  - [x] `MutabilityFacts`
  - [x] `AbiFacts`
- [x] Use phase-specific function names such as `ResolveProgram` and `ResolveMemberAccess`.
  - [x] Add `ResolveMemberAccess` RAST API.
  - [x] Add `CheckAbiShape` TAST API.
  - [x] Add `SubstituteGenericTypes` TAST API.
  - [x] Add `CheckReturnFlow` TAST API.
  - [x] Add `CheckAssignment` TAST API.
  - [x] Add `CheckCallExpression` TAST API.
- [ ] Avoid generic multi-purpose names like `ValidateThing` or broad `InferExprType` helpers that hide multiple responsibilities.

### SRP Phase 4: Structured Diagnostics

- [x] Add `Lang/include/Diagnostics/diagnostic.h`.
- [x] Add `Lang/src/Diagnostics/diagnostic.cpp`.
- [x] Add `CLI/src/diagnostic_render.cpp`.
- [x] Add `LSP/src/diagnostic_bridge.cpp`.
- [x] Define structured diagnostics with:
  - [x] diagnostic code
  - [x] source span
  - [x] phase
  - [x] message
  - [x] help text
- [x] Compiler phases should return/report diagnostics instead of only plain strings.
  - [x] Add structured validation diagnostic adapter.
- [x] CLI should render diagnostics only; it should not infer diagnostic codes from string matching.
  - [x] Move diagnostic code/help rendering out of `CLI/src/main.cpp`.
- [x] LSP should consume the same structured diagnostics as CLI.
  - [x] Route publishDiagnostics through LSP diagnostic bridge.

### SRP Phase 5: Test Split

- [x] Split VM tests out of `Tests/tests/test_System.cpp`:
  - [x] `Tests/tests/vm/test_interpreter.cpp`
  - [x] `Tests/tests/vm/test_heap.cpp`
  - [x] `Tests/tests/vm/test_gc.cpp`
  - [x] `Tests/tests/vm/test_runtime_limits.cpp`
  - [x] `Tests/tests/vm/test_native_fs.cpp`
  - [x] `Tests/tests/vm/test_native_channel.cpp`
  - [x] `Tests/tests/vm/test_jit.cpp`
- [x] Split language tests out of `Tests/tests/test_lang.cpp`:
  - [x] `Tests/tests/lang/test_lexer.cpp`
  - [x] `Tests/tests/lang/test_cast.cpp`
  - [x] `Tests/tests/lang/test_ast.cpp`
  - [x] `Tests/tests/lang/test_rast.cpp`
  - [x] `Tests/tests/lang/test_tast.cpp`
  - [x] `Tests/tests/lang/test_irb.cpp`
  - [x] `Tests/tests/lang/test_ire.cpp`
  - [x] `Tests/tests/lang/test_integration.cpp`
- [x] Split CLI tests out of `Tests/tests/test_lang.cpp`:
  - [x] `Tests/tests/cli/test_cli_contract.cpp`
  - [x] `Tests/tests/cli/test_cli_diagnostics.cpp`
  - [x] `Tests/tests/cli/test_cli_build.cpp`
  - [x] `Tests/tests/cli/test_cli_imports.cpp`
- [x] Migration order:
  - [x] heap/gc tests first
  - [x] lexer/parser tests second
  - [x] CLI tests third
  - [x] RAST/TAST tests fourth

### SRP Phase 6: CLI / Import / Build Service Split

- [x] Move import graph construction out of CLI into shared Lang/RAST service.
  - [x] Add shared RAST import path helpers.
  - [x] Add shared RAST module-map line parser.
  - [x] Add shared RAST simple-file index builder.
  - [x] Add shared RAST module index builder.
  - [x] Add shared RAST auto module-map writer.
  - [x] Add shared RAST import path resolution helpers.
  - [x] Add shared RAST local import program loader.
  - [x] Add shared RAST full import-loading entry point for CLI.
  - [x] Add shared RAST source-text import-loading entry point for LSP.
- [x] Make CLI, LSP, and tests use the same import graph implementation.
  - [x] Route CLI import path helper through shared RAST import path helpers.
  - [x] Route CLI and LSP module-map parsing through shared RAST parser.
  - [x] Route LSP simple-file index building through shared RAST builder.
  - [x] Route CLI and LSP module index building through shared RAST builder.
  - [x] Route CLI and LSP auto module-map writing through shared RAST writer.
  - [x] Route CLI and LSP import path resolution through shared RAST helpers.
  - [x] Route CLI and LSP local import program assembly through shared RAST loader.
  - [x] Route CLI full import graph construction through shared RAST loader entry point.
  - [x] Route LSP open-document import graph construction through shared RAST loader entry point.
- [x] Move CLI diagnostic rendering into `CLI/src/diagnostic_render.cpp`.
- [x] Move CLI build/embed/link helpers out of `CLI/src/main.cpp`.
  - [x] Add `CLI/src/build_contract.cpp` for build layout and embedded runner helpers.
- [x] Move CLI command parsing/dispatch into dedicated command modules.
  - [x] Add `CLI/src/command_dispatch.cpp`.
- [x] Remove CLI/LSP duplicate import wrappers after shared import graph adoption.

### SRP Phase 7: Documentation Consolidation

- [x] Replace split ownership pages with behavior-focused subsystem docs.
- [x] Consolidate language docs into `Docs/Language.md`.
- [x] Keep subsystem docs user/implementer focused rather than file-ownership focused.
- [ ] Keep `Docs/TODO.md` actionable with file-level tasks, not broad statements.

### SRP Phase 8: No-Shim / No-Facade End State

- [x] Remove legacy `lang_*.h` compatibility facades after callers move to phase headers:
  - [x] remove `lang_ast.h`
  - [x] remove `lang_parser.h`
  - [x] remove `lang_validate.h`
  - [x] remove `lang_sir.h`
- [x] Remove facade-only Lang compatibility modules from the public surface; remaining phase ownership work is tracked under [Lang Architecture](#lang-architecture).
- [x] Remove native stdlib forwarding glue once binding metadata dispatch is in place.
- [x] Remove CLI/LSP duplicate import wrappers once shared import graph is adopted.
  - Scope: source-level duplicate import helpers/wrappers are gone. Test ownership remains tracked by the open completion check below.

### SRP Completion / Regression Checks

These checks close the SRP effort only when they are true in the tree. Do not mark these complete just because the first extraction pass landed.

- [ ] `VM/src/vm.cpp` is orchestration/API only.
  - Current status: not closed; `ExecuteModule` still owns the main opcode `switch` and substantial instruction behavior.
  - Finish by moving opcode-family execution into owned interpreter/runtime modules, leaving `vm.cpp` to wire verification, state construction, execution entry, and result return.
- [ ] `Lang/src/lang_validate.cpp` is not a semantic monolith.
  - Current status: not closed; it still owns broad `InferExprType`, call checking, statement checking, type checking, and validation context behavior.
  - Progress: declaration member lookup/listing and unknown-member suggestions moved to `RAST/member_resolution`; reserved import fact resolution, native-module, member-list, module-variable type, IO print-name/print-call, DL open-call/manifest extraction, and module-expression helpers are owned by `RAST/reserved_resolution`; pure `TypeRef` construction, cloning/vector cloning, container element cloning, structural type equality, primitive/scalar type-name, builtin identifier/call-name, scalar-shape, len-compatible type classification, primitive cast syntax/argument rules, and list method-name classification moved to `TAST/types`; literal type inference, literal shape classification, fixed array literal shape validation, format placeholder counting/count validation, scalar literal compatibility, and expression/type compatibility moved to `TAST/literals`; generic parameter collection, explicit type-argument mapping, generic type substitution, generic unification, and artifact generic maps moved to `TAST/generics`; native bytecode-to-language ABI type mapping plus DL ABI type/signature checks moved to `TAST/abi`; call expression shape, procedure/function argument-count checks, single-arg builtin arity checks, call scalar-argument and printable/format argument checks, IO print format-template checks, call type-argument count checks, reserved DL open/File/IO buffer/Math/Time call argument checks, and fn-literal target type checks moved to `TAST/calls`; address-of/index expression shape and local scope lookup/insertion moved to `TAST/mutability`; addressable/member-access expression shape plus scalar, unary/binary operator, and compound-assignment operator checks moved to `TAST/expressions`; assignment-operator classification moved to `TAST/statements`; condition type rule, function return-flow rule, and switch branch value extraction moved to `TAST/control_flow`; these have direct phase-owned tests.
  - Finish by moving remaining name/member work to `RAST` and type/control/ABI/literal/expression work to `TAST`, with `lang_validate.cpp` reduced to a thin public entry point or removed if the TAST API can own it directly.
- [x] Native runtime additions use the metadata registry and include metadata/signature tests; no ad-hoc native dispatch lists or forwarding glue are reintroduced.
- [x] Current GC tracing uses declared roots/stack maps/globals rather than heuristic ref guessing.
  - Future native-handle roots belong to the layered native resource-registry work.
- [x] Lang phase APIs are direct owner APIs, not facade-only wrappers or compatibility shims.
  - CAST owns its parser API directly; legacy root parser exports and CAST namespace re-exports are gone.
<!-- Test split is intentionally not an active SRP priority right now.
- [ ] Tests are split by owning subsystem/phase.
  - Current status: not closed; `Tests/tests/test_lang.cpp` still contains broad validation/runtime/CLI integration coverage.
  - Progress: module-header CLI check, basic `svm emit/check/build` command tests, compile/build executable tests, `simple` runtime-stub command contract tests, CLI diagnostic-format checks, exit-code checks, CLI import command regressions, and import stress command checks moved to `Tests/tests/cli/`.
  - Finish by moving remaining unit/phase coverage into `Tests/tests/lang/`, VM coverage into `Tests/tests/vm/`, CLI coverage into `Tests/tests/cli/`, and leaving only true end-to-end language integration in `test_lang.cpp`.
-->

## High Priority: Layered Native Library Model

After the SRP/module-boundary work, replace the current flat low-level reserved native library with a layered native library architecture suitable for a general-purpose language and future AOT.

Target model:

```txt
VM Native Core
  internal host primitives, handle registry, traps, cleanup, capability checks

System Layer
  low-level public APIs close to OS/runtime concepts

Std Layer
  ergonomic general-purpose APIs built on System

Domain Libraries
  optional higher-level packages: net, http, json, fs, process, graphics, etc.
```

Design rules:

- [ ] Raw native capabilities must not be the default user-facing API.
- [ ] VM native core owns host resources through typed handles, not raw integers/pointers in user code.
- [ ] Every native resource must support:
  - [ ] owned handle representation
  - [ ] explicit early release where useful (`close`, `dispose`, `cancel`, `join`, etc.)
  - [ ] guaranteed VM-exit cleanup for unreleased owned handles
  - [ ] controlled double-close behavior
  - [ ] controlled use-after-close diagnostics/traps
- [ ] AOT and interpreter must share the same native ABI/resource contract.
- [ ] Low-level APIs belong under `System.*`; high-level convenience APIs belong under `Std`/top-level standard modules.
- [ ] Keep raw VM/native entry points internal unless deliberately exposed as unsafe/system APIs.
- [ ] Document capability/security policy before exposing process, socket, filesystem mutation, dynamic loading, or thread primitives as stable public APIs.

Layer ownership tasks:

- [ ] Add VM native resource registry:
  - [ ] typed native handle id
  - [ ] handle kind enum (`file`, `dir`, `socket`, `process`, `thread`, `channel`, `dl`, `buffer`, `timer`, etc.)
  - [ ] close/finalize callback per handle kind
  - [ ] ownership flags
  - [ ] closed-state tracking
  - [ ] VM shutdown cleanup sweep
  - [ ] tests for leak cleanup, double close, and use-after-close
- [ ] Split native implementation into layers:
  - [ ] `VM/native/core` or equivalent for raw host operations and registry internals
  - [ ] `System.*` reserved/native modules for explicit low-level APIs
  - [ ] `Std.*` or top-level standard modules for ergonomic wrappers
- [ ] Update native metadata to carry layer/safety/resource facts:
  - [ ] layer: `core`, `system`, `std`
  - [ ] safety/capability tags
  - [ ] resource kind produced/consumed
  - [ ] cleanup behavior
  - [ ] blocking/non-blocking behavior
  - [ ] platform availability
- [ ] Update generated native/stdlib docs to group APIs by layer instead of one flat native surface.
- [ ] Update Lang reserved module resolution to distinguish internal VM native symbols from public `System`/`Std` APIs.
- [ ] Add migration tests proving user-facing fixtures use the intended high-level or `System` layer, not raw VM-native internals.

Resource-family migration plan:

- [ ] Files/directories/path:
  - [ ] core host file/dir handles
  - [ ] `System.FS` low-level open/read/write/close/list/cwd APIs
  - [ ] high-level `FS.readText`, `FS.writeText`, `Path.join`, etc.
- [ ] Dynamic libraries/FFI:
  - [ ] core DL handle registry integration
  - [ ] `System.DL` explicit low-level open/sym/close/error APIs
  - [ ] high-level managed library/symbol wrapper with VM-exit cleanup
- [ ] Channels/threads/jobs:
  - [ ] core scheduler/job/thread/channel handles
  - [ ] `System.Thread`/`System.Channel` explicit low-level APIs
  - [ ] high-level `Thread.run`, `Channel<T>` style wrappers when generic/runtime type support is ready
- [ ] Buffers/bytes:
  - [ ] core native buffer handles or heap-owned byte arrays
  - [ ] `System.Buffer` low-level binary read/write/slice APIs
  - [ ] high-level `Bytes` conveniences
- [ ] Network/process/timers/watchers:
  - [ ] design resource ownership before implementation
  - [ ] expose low-level `System.*` first only after capability policy exists
  - [ ] add high-level `Net`, `Http`, `Process`, `Timer` APIs after stable handle cleanup exists
- [ ] JSON/log/random/time/env/os:
  - [ ] classify existing APIs as pure/stateless vs resource-owning
  - [ ] move low-level calls to `System.*`
  - [ ] keep ergonomic wrappers in top-level/high-level modules

Migration rule: do not preserve permanent forwarding shims from old flat reserved modules. Temporary adapters are allowed only inside an active migration commit series and must be removed before the section is complete.

## High Priority: Native SVM Standard Library

All Simple standard/core library modules should be implemented through the layered native model above. They should not be implemented as application DL libraries. Low-level reusable pieces belong in VM Native Core/System; ergonomic general-purpose APIs belong in Std/top-level standard modules.

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
  - [x] `Lexer`
  - [x] `CAST`
  - [ ] `AST`
  - [ ] `RAST`
  - [ ] `TAST`
  - [ ] `IRB`
  - [x] `IRE`
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
- [x] Remove legacy include facades after migration:
  - [x] remove `lang_ast.h`
  - [x] remove `lang_parser.h`
  - [x] remove `lang_validate.h`
  - [x] remove `lang_sir.h`
  - [x] update all project includes to phase headers
  - [x] delete compatibility tests that only exist to preserve legacy facades

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
- [x] Harden GC root tracing with stress tests for:
  - [x] nested lists of strings
  - [x] arrays of artifacts
  - [x] closures capturing refs
  - [x] artifact fields containing refs
  - [x] switch/loop local lifetimes with refs
  - [x] globals holding refs
  - [x] temporary stack refs during calls
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
- [x] Define `svm` and `simple` behavior precisely; root `bin/` publishes only those two.
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
  - [x] `test_lexer.cpp`
  - [x] `test_cast.cpp`
  - [x] `test_ast.cpp`
  - [x] `test_rast.cpp`
  - [x] `test_tast.cpp`
  - [x] `test_irb.cpp`
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
- [x] Remove obsolete sprint log from the active docs set.
