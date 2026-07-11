# Simple Timeline

This is the canonical project roadmap. Native library prerequisites and native-library features take priority over older backlog items whenever priorities conflict.

Order of work:

1. Stabilize the current interpreter/CLI/test baseline.
2. Complete compiler / IR / bytecode prerequisites for language-neutral VM types and operations.
3. Complete the language surface: monomorphic generics, closures, `Result`/`Option`/`Promise`, and resource-safe control flow.
4. Native ABI and language-level result/option/promise/handle types.
5. VM Native Core resource registry and safety model.
6. LLVM ORC JIT broad Tier 0, then optimized Tier 1.
7. Canonical library namespace/runtime naming stabilization: remove lowercase runtime modules and internal short forms.
8. Low-level `System.*` APIs.
9. High-level `Standard.*` APIs. Public short aliases are not part of the final model.
10. Documentation, generated API references, and cross-platform tests.
11. Lower-priority SRP/backlog cleanup only when it directly supports the above.

Locked architecture decisions:

- The interpreter is the semantic baseline. The LLVM ORC JIT may only accelerate validated SBC and must fallback or trap safely when unsupported.
- SBC remains the portable artifact. JIT output is platform-local cacheable machine code, not a distribution format.
- `data` is stable-layout ABI data. `artifact` is a managed language object. Methods never affect layout.
- Generics are compile-time monomorphic. Every concrete instantiation is type-checked and emitted as a concrete specialized declaration.
- Generic arguments may be any type with stable type identity: primitives, strings, arrays, lists, data, artifacts, enums, pointers, functions, handles, results/options/promises, and instantiated generic types.
- Native calls are metadata-driven. Interpreter, JIT, future AOT, docs, and language reserved signatures must consume the same native-function metadata.
- Host resources are generational opaque handles. Raw platform handles and VM heap internals are never exposed directly to Simple code.
- `System.*` is explicit, low-level, capability-aware, native/runtime-facing, and returns `Result`/`Option` for expected host failures. `Standard.*` is ergonomic and wraps `System.*`. The final public and internal library model has no short compatibility aliases, no lowercase runtime module names, and no duplicate root modules; source, compiler internals, native registry metadata, LSP, docs, tests, and generated references use only canonical `System.X` or `Standard.X` names.

---

## Phase 0: Compiler / IR / Bytecode Prerequisites

These lower layers remain language-neutral. They define VM-level type metadata, bytecode operations, verification, and lowering only; language sugar and native library names are tracked in later phases.

### Type-System Prerequisites

- [x] Add generic VM metadata for result-like, option-like, vector, aggregate, function, and pointer types.
- [x] Keep artifact-as-struct layout explicit in SIR/SBC type metadata.
- [x] Keep artifact methods separate from artifact layout metadata.
- [x] Add opaque handle/resource metadata without naming language or native-library modules.
- [ ] Add type-extension rows for resource kind, capability tags, blocking behavior, and platform availability.

### IR / SIR Prerequisites

Language-neutral SIR syntax/lowering status:

- [x] Module/version/export metadata.
  - [x] `sir version <major>.<minor>`.
  - [x] `module <name>`.
  - [x] `export <symbol> <func> [flags=<u32>]`.
  - [x] source debug rows: `file`, `line`, `span`, `symbol`.
- [x] Local/upvalue metadata.
  - [x] `local <name> <type> <slot>`.
  - [x] `upvalue <name> <type> <slot>`.
- [x] Constants/data.
  - [x] `const <name> bytes "..."`.
  - [x] `const <name> data <blob>`.
  - [x] `const <name> array<T> [...]`.
  - [x] `const.bytes <const>`.
  - [x] `const.data <const>`.
  - [x] `load.dataref <const>`.
- [x] Module lifecycle.
  - [x] `init.module <module>`.
  - [x] `ensure.module.init <module>`.
- [x] Native/import calls.
  - [x] `call.import <import> <argc>`.
  - [x] `call.native <native> <argc>`.
- [x] String/bytes conversion.
  - [x] `string.to.bytes`.
  - [x] `bytes.to.string`.
- [x] Result-like/option-like/variant operations.
  - [x] `variant.tag`.
  - [x] `variant.payload.<T> <case>`.
  - [x] `variant.make.<T> <case>`.
  - [x] `result.ok.<T>`.
  - [x] `result.err.<T>`.
  - [x] `result.is.ok`.
  - [x] `result.is.err`.
  - [x] `result.unwrap.<T>`.
  - [x] `result.propagate.err`.
  - [x] option-like metadata is represented without language-specific spelling.
- [x] Async/concurrency operations.
  - [x] `spawn <func>`.
  - [x] `await`.
  - [x] `future.make <func>`.
  - [x] `future.poll`.
  - [x] `channel.send.<T>`.
  - [x] `channel.recv.<T>`.
  - [x] `channel.try.recv.<T>`.
- [x] Capability/security syntax.
  - [x] `cap.check <capability>`.
  - [x] sandbox enter/exit forms.
- [x] Preserve generic VM type arguments in SIR/SBC metadata where represented.
- [ ] Preserve opaque handle resource kind through validation, SIR, SBC metadata, and native dispatch.
  - [x] SBC loader validates opaque handle resource kind metadata.
  - [x] Runtime ABI classifier maps opaque handle type rows to handle ABI values.
  - [x] Bytecode verifier accepts opaque handle values as packed handle words.
  - [x] SBC loader rejects conflicting type layout flags.
  - [x] Native resource kinds expose stable ids for opaque handle metadata.
  - [x] Native layer maps opaque SBC type rows back to resource kinds.

### Bytecode / SBC Prerequisites

Language-neutral SBC metadata/opcodes status:

- [x] Type/metadata rows.
  - [x] result-like type row.
  - [x] option-like type row.
  - [x] opaque handle/resource metadata.
  - [x] future/task-handle metadata.
  - [x] `Module` section.
  - [x] `Data` section.
  - [x] `Capabilities` section.
  - [ ] type-extension rows for resource kind, capability tags, blocking behavior, and platform availability.
- [x] Constants/data opcodes.
  - [x] `ConstBytes`.
  - [x] `ConstData`.
  - [x] `LoadDataRef`.
- [x] Module lifecycle opcodes.
  - [x] `InitModule`.
  - [x] `EnsureModuleInit`.
- [x] Native/import call opcodes.
  - [x] `CallImport`.
  - [x] `CallNative`.
  - [x] `CallMethod`.
  - [x] `CallVirtual`.
- [x] String/bytes conversion opcodes.
  - [x] `StringToBytes`.
  - [x] `BytesToString`.
- [x] Pointer/unsafe/system opcodes.
  - [x] `AddressOfLocal`, `AddressOfGlobal`, `AddressOfField`.
  - [x] `LoadPtr<T>`, `StorePtr<T>`.
  - [x] `PtrAdd`, `PtrOffset`, `PtrEq`, `PtrNe`, `PtrIsNull`.
  - [x] `PtrCheckNull`, `PtrCheckBounds`.
  - [x] `MemCopy`, `MemMove`, `MemSet`, `MemCompare`.
- [x] Checked/safe opcodes.
  - [x] `CheckedConv<From,To>`.
  - [x] `CheckedArrayGet<T>`, `CheckedArraySet<T>`.
  - [x] `CheckedListGet<T>`, `CheckedListSet<T>`.
  - [x] `CheckedStringGetChar`, `CheckedStringSlice`.
  - [x] `CheckedNull`, `CheckedBounds`, guard opcodes.
- [x] Result-like/option-like/variant opcodes.
  - [x] `VariantTag`, `VariantPayload<T>`, `VariantMake<T>`.
  - [x] `ResultOk<T>`, `ResultErr<T>`.
  - [x] `ResultIsOk`, `ResultIsErr`.
  - [x] `ResultUnwrap<T>`.
  - [x] `ResultPropagateErr`.
  - [x] option-like values represented through metadata/variant surface.
- [x] Concurrency/future/channel opcodes.
  - [x] `Spawn`, `Join`, `Detach`.
  - [x] `Await`, `Yield`, `Resume`, `Suspend`.
  - [x] `MakeFuture`, `PollFuture`.
  - [x] `ChannelSend<T>`, `ChannelRecv<T>`, `ChannelTryRecv<T>`.
- [x] Runtime/GC coordination opcodes.
  - [x] `Safepoint`, `AllocCheckpoint`.
  - [x] `WriteBarrier`, `ReadBarrier`.
  - [x] `PinRef`, `UnpinRef`, `KeepAlive`.
- [x] Capability/security opcodes.
  - [x] `CheckCapability`.
  - [x] `EnterSandbox`.
  - [x] `ExitSandbox`.
- [x] Verifier checks.
  - [x] payload/result/variant stack correctness.
  - [ ] wrong resource kind for opaque handle use.
  - [x] native call result type matches metadata where metadata is available.
  - [x] explicit result-like / option-like extraction stack correctness.
  - [x] propagation opcode stack correctness.

### Documentation Prerequisites

- [x] Update `Docs/IR.md` with implemented language-neutral IR syntax.
- [x] Update `Docs/Byte.md` with implemented SBC rows for VM-level opcodes and metadata.

---

## Phase 0.5: Language Completion and Monomorphic Generics

This phase finishes the language surface that higher runtime and JIT work depends on. It must preserve phase boundaries: parsing owns syntax, RAST owns names/imports, TAST owns semantic facts, a new specialization phase owns generic instantiation, and IRB/IRE only lower concrete typed declarations.

### Current Baseline Stabilization

- [ ] Fix `build/bin/simplevm_tests` segfault when run from repository root.
- [x] Make `ctest` execution independent of current working directory or force repository-root execution in CTest configuration.
- [ ] Keep `svm run/check/emit/build/lsp` interpreter-mode tests green before expanding language/JIT features.
- [ ] Split current LLVM ORC scaffolding into reviewable commits before adding broad feature lowering.

### Generic Type Model

- [x] Define canonical type identity for every generic argument kind.
  - [x] primitives.
  - [x] `string`, `Bytes`, heap references, and managed artifacts.
  - [x] arrays/lists and nested aggregate types.
  - [x] stable-layout `data` types.
  - [x] enums and pointer types.
  - [x] function/procedure types including captured closures.
  - [x] `System.Handle<T>`, `Result<T,E>`, `Option<T>`, `Promise<T>`.
  - [x] `Channel<T>`.
  - [x] instantiated generic types as generic arguments.
- [x] Define deterministic generic symbol mangling.
  - [x] Human/debug form, e.g. `Map<string, List<i32>>`.
  - [x] Link/internal form with stable escaping or hash suffixes.
  - [x] Collision detection with diagnostic output.
- [ ] Add generic declaration metadata to TAST and SIR/SBC debug/type metadata.
  - [x] TAST collects generic function/data/artifact/method declaration metadata.
- [ ] Reject recursive value containment without indirection; allow recursion through pointer/ref/handle.

### Specialization Pipeline

- [x] Add `Lang/include/GEN/specializer.h`.
- [x] Add `Lang/src/GEN/specializer.cpp`.
- [ ] Insert specialization after generic TAST validation and before IRB lowering.
  - [x] GEN exposes whole-program specialization plan construction from AST/TAST metadata.
  - [x] IRB materializes GEN specializations before SIR emission/lowering.
- [x] Collect generic function/data/artifact declarations.
- [ ] Collect concrete instantiation requests from call sites, type annotations, literals, fields, globals, imports, and native signatures.
  - [x] Initial GEN collector walks type annotations in declarations, fields, methods, modules, and statements.
  - [x] GEN collector records explicit generic call-site type arguments.
  - [x] GEN collector walks extern/native signature type annotations.
  - [x] GEN whole-program planning filters requests to declared user generics.
- [ ] Instantiate dependencies recursively with cycle detection.
  - [x] GEN resolves instantiation dependency order and reports cycles.
  - [x] GEN builds ordered specialization plans from dependency graphs.
- [ ] Re-run semantic/type checks on each concrete specialization.
  - [x] GEN specialization plans retain parameter-to-concrete-type bindings for semantic re-check hooks.
  - [x] GEN builds concrete substitution maps for specialization semantic re-checks.
  - [x] IRB validates materialized concrete specialization programs before SIR emission.
- [ ] Cache/reuse equivalent specializations across a module graph.
  - [x] GEN normalizes duplicate instantiation requests by deterministic request key.
  - [x] GEN preserves concrete request type metadata while deduplicating equivalent instantiations.
  - [x] GEN builds deterministic specialization plans from declarations and requests.
  - [x] GEN detects specialization symbol collisions while planning.
- [ ] Emit only concrete specialized declarations into IRB/IRE.
  - [x] GEN rejects non-concrete specialization requests during planning.
  - [x] GEN can materialize concrete function and artifact layout declarations from specialization plans.
  - [x] GEN can build a concrete-only AST program with generic declarations omitted and specializations appended.
  - [x] GEN rewrites materialized generic type/call references to specialized symbols.
  - [x] GEN rejects materialized specialization names that collide with concrete top-level declarations.

### Generic Language Surface

- [ ] Generic functions.
- [ ] Generic `data` declarations with stable specialized layout.
- [ ] Generic `artifact` declarations and methods.
- [ ] Generic methods on non-generic and generic receivers.
- [ ] Generic procedure/function value types.
- [ ] Generic list/array element types where lowering/runtime supports the concrete specialization.
- [ ] Generic native-facing types only when ABI metadata is complete.
- [ ] Operation validity checked per specialization rather than by broad predeclared constraints.
- [ ] Reserve trait/constraint syntax for later; do not block v1 generics on traits.

### Closures and Procedure Values

- [ ] Complete closure capture semantics for procedure literals.
- [ ] Represent captured closures as heap objects with exact GC roots.
- [ ] Allow procedure values in generic instantiations when the concrete storage/runtime path is supported.
- [ ] Keep procedure values rejected at external FFI boundaries unless explicitly wrapped by a native callback ABI.
- [ ] Support direct inline invocation of anonymous function literals after capture/lifetime rules are implemented.

### Result / Option / Promise Language Surface

- [ ] Define canonical `Result<T,E>` and `Option<T>` types in language-facing standard metadata.
- [ ] Define canonical `Promise<T>` language type backed by VM promise/job records.
- [ ] Add `try` propagation sugar for `Result<T,E>`.
- [ ] Add optional unwrapping/control-flow sugar after `Option<T>` representation is stable.
- [ ] Ensure result/option/promise generic instantiations lower through the monomorphic specialization path.

### Resource-Safe Control Flow

- [ ] Decide `defer` and/or scoped `using` syntax for deterministic cleanup.
- [ ] Lower cleanup edges for normal return, error propagation, break/skip, trap-safe VM unwinding where supported.
- [ ] Integrate resource cleanup with `System.Handle<T>` and native resource registry.

---

## Phase 1: Native ABI Contract

The Simple ABI has two layers. The primary ABI is the Simple Native ABI used by the VM runtime, native modules, LLVM ORC JIT helpers, and future AOT. External C FFI is a restricted boundary for declared `extern` calls and dynamic-library dispatch.

`data` declarations are stable-layout ABI structs. `artifact` declarations are managed language objects unless explicitly lowered through a separate handle/reference ABI. Fields define layout; methods are language-level functions and do not affect ABI size, alignment, field order, or calling convention.

### ABI Layers

- [ ] Define Simple Native ABI based on `NativeCallContext` and `NativeCallResult`.
  - [x] Native handlers decode arguments through typed accessors, including packed handles, canonical bool/char values, and borrowed string/byte views.
  - [x] Native handlers build returns through typed builders, including canonical bool/char values.
  - [x] Resource, capability, allocation, blocking, and GC behavior are visible through metadata.
- [ ] Define restricted External C FFI ABI.
  - [x] Permit primitives and pointers through external C ABI verifier.
  - [x] Permit stable `data` structs through external C ABI type-info verifier.
  - [x] Permit explicit ABI wrapper types.
  - [x] Reject managed artifacts, closures, VM heap internals, and implicit `string -> char*` coercions in external C ABI verifier.
  - [x] Use libffi/platform ABI only after Simple ABI metadata and layout checks pass.
    - [x] Runtime verifier accepts only external-callable primitive/pointer/stable aggregate type infos.
    - [x] Runtime verifier accepts explicit external C wrappers for C string, string view, and bytes view.
    - [x] Dynamic library call dispatch gates libffi setup behind external ABI type-info validation.
- [ ] Keep interpreter, LLVM ORC JIT, and future AOT on the same Simple Native ABI contract.
  - [x] LLVM loop-call acceptance for native/import calls uses canonical native metadata validation instead of duplicating signature/effect checks in the backend.

### Exact ABI Mapping Table

- [ ] Define exact ABI mapping table for every native-callable type.
  - [x] Add runtime primitive ABI classifier for current SBC `TypeKind` values.
- [ ] Scalar representations:
  - [x] signed/unsigned integers use exact-width two's-complement payloads.
  - [x] floats use IEEE-754 `f32`/`f64`.
  - [x] `bool` ABI is `u8` with only `0` or `1` valid.
    - [x] Runtime ABI value validator rejects non-canonical bool payloads.
  - [x] `char` ABI is `u32` Unicode scalar long-term; document current bytecode compatibility if still 16-bit internally.
    - [x] Runtime ABI value validator rejects invalid Unicode scalar payloads.
  - [x] enums use declared or default underlying integer type.
- [ ] Reference/handle representations:
  - [x] VM heap references are opaque VM refs, never raw host pointers.
  - [x] `System.Handle<T>` lowers to `NativeHandleId` packed as a VM word.
  - [x] `Promise<T>` lowers to a generational promise/job id.

### Stable `data` Layout ABI

- [x] Field declaration order is layout order.
- [x] Alignment rules:
  - [x] 1-byte: `bool`, `i8`, `u8`.
  - [x] 2-byte: `i16`, `u16`.
  - [x] 4-byte: `i32`, `u32`, `f32`, `char`, enum32.
  - [x] 8-byte: `i64`, `u64`, `f64`, pointer, ref, handle.
  - [x] nested `data`: max field alignment, capped at 8 initially.
- [x] Padding is deterministic and zero-initialized where observable through ABI.
- [x] Total size rounds up to max alignment.
- [x] Nested `data` structs are allowed after recursive layout validation in runtime ABI layout helpers.
- [x] Runtime ABI maps stable SBC data type rows through recursive field layout classification.
- [x] Recursive value containment is rejected; recursive pointer/ref/handle containment is allowed.
- [x] By-value internal passing for no-ref structs at or below the chosen small aggregate threshold, initially `<= 16` bytes.
- [x] Larger/ref-containing aggregates pass by readonly pointer or VM-managed reference according to metadata.
- [x] Return-by-value rules match parameter rules through explicit ABI pass-mode helpers.
- [x] Tests prove methods do not affect layout.
- [ ] Tests cover padding, nested data, arrays, generics, and layout hashes.
  - [x] Runtime ABI tests cover deterministic stable aggregate layout hashes.
  - [x] Runtime ABI tests cover primitive padding and ref-containing aggregate classification.
  - [x] Runtime ABI layout records zero-initialized padding ranges.
  - [x] Runtime ABI tests cover parameter/return pass-mode classification.
  - [x] Runtime ABI tests cover nested aggregate layout.
  - [x] Runtime ABI tests cover recursive value-containment rejection.
  - [x] Runtime ABI tests cover method-insensitive stable data layout.
  - [x] Runtime ABI tests cover fixed array layout.

### Strings and Bytes ABI

- [x] Define `SimpleStringView { data, size, encoding }` for borrowed string arguments.
- [x] Add native-call typed accessor for borrowed UTF-8 string views.
- [x] Strings are UTF-8 at ABI boundaries unless a different encoding is explicit.
- [x] Borrowed string/bytes views are valid only for the native call duration.
- [x] Native functions returning strings allocate/build VM-owned strings.
- [x] Define heap-owned `Bytes` as the canonical immutable byte sequence.
- [x] Define `SimpleBytesView { data, size }` for borrowed bytes arguments.
- [x] Add native-call typed accessor for borrowed heap `Bytes` views.
- [ ] Define mutable buffers as resource handles, not borrowed mutable VM internals.
- [x] External FFI verifier requires explicit `cstring`, pointer, or bytes wrapper types; no implicit `string` coercion.

### Handles, Results, Options, Promises

- [x] Define `NativeHandleId { index, generation }` as the opaque VM payload for `System.Handle<T>`.
- [x] Native callees declare expected resource kind; metadata dispatch validates kind/generation/closed state before use.
- [x] Native call context exposes typed resource-handle validation for handlers.
- [x] Define canonical `Result<T,E>` representation.
  - [x] tag plus inline small payload or heap box for large/ref-containing payloads.
  - [x] native handler builders for ok/err.
  - [x] external FFI rejects generic `Result` unless explicitly wrapped.
- [x] Define canonical `Option<T>` representation.
  - [x] tag plus payload; nullable reference optimization may be added only when semantics remain identical.
  - [x] native handler builders for some/none.
- [ ] Define `Promise<T>` runtime representation.
  - [x] promise/job id payload.
  - [x] pending/done/failed/canceled states.
  - [x] cancellation flag and waiter list.
  - [x] result payload roots and GC behavior.

### ABI Metadata and Tests

- [x] Add ABI classification helper for scalar, float, ref, aggregate, variant, promise, and opaque classes.
- [x] Add layout hash for stable `data` types and generic specializations.
- [x] Add ABI verifier checks for native-callable signatures.
- [x] Add native JIT-call ABI classification tests for metadata validity, signature matching, root needs, blocking/allocation effects, and resource mutation rejection.
- [ ] Add interpreter/JIT shared tests for the same native ABI calls.
- [ ] Document all mappings in `Docs/Language.md`, `Docs/VM.md`, `Docs/IR.md`, and `Docs/Byte.md`.

---

## Phase 2: VM Native Core

### Resource Registry

- [x] Add `VM/include/native/resource_registry.h`.
- [x] Add `VM/src/native/resource_registry.cpp`.
- [x] Define `NativeHandleId` as opaque VM value payload.
- [ ] Map each public `System.Handle<T>` to `NativeHandleId` plus resource kind/generation.
  - [x] Runtime ABI maps opaque handle type rows to packed `NativeHandleId` values.
  - [x] Back legacy file descriptors with `NativeResourceRegistry` handles internally.
  - [x] Remove legacy `open_files` native dispatch storage.
- [x] Define `NativeResourceKind`:
  - [x] stable numeric ids for SBC opaque handle metadata.
  - [x] file, directory, socket, listener, process, thread, job, channel.
  - [x] FFI library/symbol.
  - [x] ASM unit/object/symbol.
  - [x] buffer, timer, watcher, terminal.
- [x] Define `NativeResourceRecord`:
  - [x] kind.
  - [x] generation/version.
  - [x] ownership flags.
  - [x] closed flag.
  - [x] debug label.
  - [x] platform handle payload.
  - [x] close/finalize callbacks.
- [x] Validate handle kind before every use.
- [x] Detect stale handle generation after close/reuse.
- [ ] Define behavior for:
  - [x] double close.
  - [x] use after close.
  - [x] wrong-kind handle use.
  - [x] VM shutdown with live handles.
  - [x] native callback failure during cleanup.
- [x] Add VM shutdown cleanup sweep.
- [ ] Tests:
  - [x] leak cleanup on registry shutdown.
  - [x] double close.
  - [x] use-after-close.
  - [x] wrong-kind handle.
  - [x] stale reuse detection.
  - [x] shutdown close failure count with finalization.

### Native Metadata

Extend `NativeFunctionSpec` with:

- [x] layer: `core`, `system`, `standard`, `domain`.
- [x] module and function name.
- [x] parameter/return types.
- [x] resource inputs/outputs metadata shape.
- [x] ownership transfer rules beyond input/output/input-output access.
- [x] cleanup behavior metadata beyond resource kind/use.
- [x] blocking behavior metadata shape.
- [x] allocation behavior metadata shape.
- [x] GC/safepoint behavior metadata shape.
- [x] capability tags metadata shape.
- [x] platform availability metadata shape.
- [x] stability status metadata shape.
- [x] doc summary metadata shape and generated output.
- [x] metadata validator for required names, handlers, parameter types, resources, ownership, and cleanup.

### Capability Policy

- [ ] Add capability tags:
  - [x] filesystem read/write.
  - [x] path metadata filesystem queries.
  - [ ] process spawn.
  - [x] process argument access.
  - [x] environment read/write.
  - [ ] network client/server.
  - [x] FFI/dynamic loading.
  - [ ] native assembly/code generation.
  - [x] threading.
  - [x] clock/time.
  - [x] randomness.
  - [ ] terminal control.
- [x] Add default allow-all CLI/VM capability policy with explicit deny-list-capable execution options.
- [ ] Add stricter sandbox policy later.
- [x] Document stable vs unsafe/system APIs in native metadata and generated docs.
- [x] Enforce native metadata capability tags during metadata dispatch.

---

## Phase 2.5: LLVM ORC JIT Completion

The LLVM ORC JIT is an optional execution backend over verified SBC. CLI execution remains interpreter-first until Tier 0 coverage and root/safepoint behavior are broad enough to make JIT the default. `-int` must always force interpreter execution for debugging and parity.

### JIT ABI and Runtime Interface

- [x] Define `JitCallContext` as the stable internal ABI for compiled Simple functions.
  - [x] argument access.
  - [x] return slot.
  - [x] operand stack slots.
  - [x] locals/spill slots.
  - [x] globals access.
  - [x] heap/runtime helper access.
  - [x] trap/error builder.
  - [x] safepoint/root registration.
- [x] Define `JitStatus` result codes for halt, return, trap, fallback, and unsupported.
- [x] Aggregate JIT status counts in execution stats.
- [x] Keep helper-heavy context ABI as the correctness path. Direct scalar native signatures are optimization-only.
- [x] Cache ORC entries by module id, function id, code hash, ABI version, and runtime helper ABI version.

### Tier 0: Broad Safe LLVM Lowering

- [ ] Lower all scalar constants, locals, globals, arithmetic, comparisons, casts, and branches.
- [ ] Lower loops with validated stack/local merge states.
- [ ] Lower direct Simple calls through `JitCallContext` dispatch or compiled entry lookup.
- [ ] Lower indirect/import/native calls through shared metadata dispatch helpers.
- [ ] Lower heap operations through runtime helpers: strings, arrays, lists, artifacts, closures, result/option boxes.
- [ ] Lower bounds/null/type checks to runtime trap helpers with equivalent interpreter diagnostics.
- [ ] Add helper calls for allocation checkpoints, safepoints, write barriers, read barriers, pin/unpin, keepalive.
- [ ] Fallback cleanly for unsupported opcodes/shapes without corrupting VM state.

### GC and Safepoints

- [ ] Register JIT frames with explicit root slots before any helper that may allocate or block.
- [x] Publish explicit root refs from `JitCallContext` args, locals, and operand stack slots using SBC type metadata.
- [x] Snapshot caller locals and operand stack into `JitCallContext` for LLVM helper dispatch roots.
- [x] Publish exact known operand-stack root masks for LLVM helper dispatch snapshots.
- [x] Publish exact known local root masks at helper call sites, including conservative branch-merge invalidation.
- [x] Register published JIT helper roots with GC tracing while helper dispatch is active.
- [x] Scope unsafe loop-call rejection to calls inside backward-branch ranges so pre-loop native setup does not block hot-loop JIT.
- [x] Include native/import/direct call target labels in LLVM loop-call rejection diagnostics.
- [x] Treat unspecified-return direct Simple calls as void-safe for scalar loop-call lowering.
- [x] Route accepted scalar dynamic `System.FFI` loop calls through a specialized LLVM helper instead of full helper dispatch.
- [x] Remove dynamic `System.FFI` scalar direct-bind shims; scalar loop calls use the canonical helper path.
- [x] Remove raylib-specific dynamic FFI direct binds from LLVM; dynamic FFI loop acceptance now goes through generic VM ABI/native ABI gates only.
- [x] Include rejected loop-call SBC signature shapes in LLVM JIT diagnostics.
- [x] Route scalar dynamic FFI LLVM helper calls through `JitCallContext` snapshots/root publication.
- [x] Bump the LLVM runtime-helper ABI version after changing the dynamic FFI helper signature.
- [x] Allow verified dynamic FFI borrowed C-string input calls inside LLVM loops through the `JitCallContext` helper path.
- [x] Include loop bytecode ranges in unsafe LLVM loop-call rejection diagnostics.
- [x] Keep dynamic FFI helper dispatch rooted in `JitCallContext` argument/heap/trap state instead of library-specific direct-bind shims.
- [x] Publish caller local/operand roots and safepoint metadata around accepted dynamic FFI helper paths.
- [x] Route scalar dynamic FFI loop calls through `JitCallContext` helper dispatch instead of raw LLVM C calls.
- [x] Validate dynamic FFI VM/native ABI signatures through the canonical FFI verifier before LLVM loop-call acceptance.
- [x] Classify dynamic FFI ABI validation by VM marshal support, native ABI validity, root needs, allocation risk, helper safety, and LLVM loop safety.
- [x] Add an interpreter-vs-JIT reduced repro for scalar native/import calls inside loops.
- [x] Add an interpreter-vs-JIT reduced repro for scalar dynamic FFI loop calls.
- [x] Include aggregate field layout fingerprints in LLVM loop-call signature diagnostics.
- [ ] Keep exact root facts for captured refs and temporary helper values.
- [x] Add safepoint metadata to lowered helper call sites, including metadata-derived blocking/allocation flags.
- [ ] Add safepoint metadata to loop backedges.
- [ ] Add tests that force GC from JIT helper calls.
- [ ] Later: replace coarse root slots with LLVM stackmap/root-map integration.

### Native Calls from JIT

- [x] CLI JIT stats include function names so hot native/import loop blockers can be identified.
- [ ] JIT native calls use `NativeFunctionSpec` id and the same dispatcher as the interpreter.
- [ ] Capability checks run identically in interpreter and JIT.
- [x] Resource kind/generation/closed-state checks run through shared metadata dispatch before native handlers.
- [ ] Blocking/native calls publish safepoints and roots before entering host code.
- [x] Scalar/void direct and import helper calls are allowed through the shared helper ABI inside LLVM-lowered loops.
- [x] Indirect/procedure calls inside loops reject until exact target metadata/effects are available.
- [x] Ref/string/resource direct calls inside loops reject until caller-frame roots/safepoints are complete.
- [x] Native import loop enabling is gated by canonical native metadata validation: valid handler/signature metadata, matching SBC signature, non-blocking, no allocation, no GC safepoint, and no mutating/output resources.
- [x] Managed string/ref arguments are allowed for safe native import loop calls via `JitCallContext` roots.
- [x] Borrowed resource input arguments are allowed for safe native import loop calls while output/mutating resources still reject.
- [x] Dynamic `System.FFI` loop enabling requires scalar/void signatures that pass the external C ABI verifier.
- [x] Direct native binding is allowed only for pure, non-blocking, non-allocating, no-resource helpers after metadata marks them safe.

### Tier 1: Optimized Monomorphic Lowering

- [ ] Emit multi-function LLVM modules for hot call groups.
- [ ] Inline monomorphic Simple calls where recursion and code size policy permit.
- [ ] Optimize monomorphized generic specializations as normal concrete functions.
- [ ] Scalar-replace small stable `data` values.
- [ ] Inline small aggregate field access and small fixed-array access where bounds are proven.
- [ ] Add bounds-check elimination after verifier/type facts are available.
- [ ] Add direct fast paths for pure runtime helpers while preserving fallback.

### JIT Parity and Diagnostics

- [ ] Every JIT-supported opcode has interpreter parity tests.
- [x] Every JIT fallback reason is counted and optionally printed by `--jit-stats`.
- [x] `--jit-stats` reports function names alongside function indexes.
- [x] LLVM loop-call rejection reasons include opcode, bytecode pc, call category, and safety reason.
- [ ] Runtime traps include opcode/function/pc/source debug context when available.
- [ ] Test `-jit` and `-int` produce identical user-visible results for fixture suites.
- [x] Remove old compiled-runner code; JIT work targets LLVM ORC plus interpreter fallback only.

---

## Phase 3: Canonical Library Namespace and Runtime Naming Stabilization

This phase must complete before new library breadth work. The goal is to make the no-alias model real everywhere, not only at the public parser boundary.

Final rule:

```simple
import System.X
import Standard.X
```

Rejected forever:

```simple
import IO
import FS
import DL
import Time
import Buffer
import Channel
```

Canonical naming requirements:

- [x] Native registry module names use final public spelling exactly, e.g. `System.FS`, `System.FFI`, `System.Buffer`, `System.OS`, `System.IO`, `System.Channel`; lowercase runtime names are rejected as stale serialized/import metadata.
- [ ] Compiler reserved-import internals use enum IDs; remaining transitional canonical-string APIs are being retired from downstream call sites. The catalog has moved behind the shared `Library/include/library_catalog.h` boundary with `Lang/include/lang_library.h` kept as a compatibility shim. `v0.4.64` adds central `LibrarySignatureSpec` / `LibraryParamSpec` / `LibraryTypeSpec` metadata and routes LSP reserved signature help through it; `v0.4.65` routes TAST reserved call target construction through the same metadata. `v0.4.67` starts Phase 4 by moving legacy import/member/runtime-name compatibility helpers into `Library/src/library_legacy.cpp`; `v0.4.68` also routes legacy serialized runtime module replacement (`System_os`, `System_fs`, etc.) through that quarantine file.
- [ ] RAST/TAST/SIR/IR/import metadata stores `System.X` / `Standard.X`, never short internal aliases.
- [x] Native import lowering emits canonical module names and symbols; stale lowercase compatibility execution is rejected.
- [ ] LSP completions, hover, signature help, semantic tokens, document links, snippets, and diagnostics use canonical names only.
- [ ] Docs, README, examples, playgrounds, Website samples, tests, generated stdlib references, and editor assets contain no lowercase runtime modules and no legacy import examples except explicit rejection diagnostics.
- [ ] Public byte/memory modules use the final four-part model where implemented: `System.Buffer` for low-level mutable native/runtime buffers, `System.Bytes` for low-level immutable/owned byte values, `Standard.Buffer` for ergonomic growable/cursor buffers, and `Standard.Bytes` for ergonomic byte-value conversion/helpers.
- [ ] Legacy imports fail with targeted suggestions and tests for every legacy name: `IO`, `FS`, `DL`, `Time`, `Buffer`, `Channel`, `Math`, `OS`, `File`, `Path`, `Env`, `Random`, `Json`, `Log`, `Thread`, `Http`, `Socket`.
- [ ] Duplicate root behavior is rejected: unimplemented `Standard.*` modules must not expose raw `System.*` members as placeholders.
- [ ] Transitional high-level members exposed under `System.*` are removed or moved: `System.FS.readText/writeText/readBytes/writeBytes`, `System.Random.range`, `System.Log.info/warn/error`, `System.Time/OS.formatWallNs`.
- [x] Runtime module-name migration is versioned through `v0.4.63`: old serialized/native module names are rejected with diagnostics or migrated at build time; no silent alias execution. CMake now rejects stale `SIMPLEVM_VERSION_OVERRIDE` cache values that do not match `VERSION`.
- [ ] Full baseline is green after namespace migration: `ctest`, `svm check/run` fixtures, LSP tests, docs link checks where available. Current early runtime/native sections pass, but known IR text/emission failures remain tracked separately.

Native metadata contract for every final `System.*` function:

- [x] Native registry entries carry enum-backed `LibraryModuleId` metadata when their module is a cataloged `System.*` / `Standard.*` module.
- [ ] `layer = system`.
- [ ] canonical `module`, canonical `symbol`, and exact Simple signature. `v0.4.66` validates native specs against catalog signatures when a catalog signature exists for the registered `LibraryModuleId`/symbol.
- [ ] resource inputs/outputs, ownership transfer, cleanup behavior, blocking behavior, allocation behavior, and GC safepoint behavior.
- [ ] capability tags, platform availability, stability, and doc summary.
- [ ] JIT/direct-call safety derived only from metadata.
- [ ] Metadata validation fails CI for missing docs, capabilities on host-touching APIs, wrong layer, wrong module casing, lowercase module names, or short internal module names.

---

## Phase 4: Low-Level `System.*` APIs

`System.*` is explicit, low-level, capability-aware, native/runtime-facing, and close to the Simple VM ABI. It exposes typed handles, explicit cleanup, clear blocking behavior, capability checks, and `Result<T>` / `Option<T>` for expected failures or absence.

Sync/async convention:

- Sync APIs: `System.Domain.function(...) -> Result<T>` / `Option<T>`.
- Async APIs, when present: `System.Domain.async.function(...) -> Promise<T>`.
- Do not use `Async` suffixes or capital `Async` modules.
- No library-specific shims, no implicit ABI guessing, and no duplicate ergonomic APIs under `System.*`.

Required `System.*` modules:

```txt
System.IO
System.FS
System.Path
System.Env
System.OS
System.Time
System.FFI
System.ASM
System.Buffer
System.Bytes
System.Json
System.Log
System.Random
System.Thread
System.Job
System.Channel
System.Process
System.Net
System.HTTP
System.Terminal
System.Capability
System.Runtime
System.Debug
```

Byte/memory naming is intentional: `Buffer` means mutable/native/runtime/cursor-oriented storage; `Bytes` means owned byte values. Implement both only when both abstractions exist, but reserve their meanings now.

### `System.IO`

- [ ] `IOHandle` backed by generational native resources.
- [ ] `stdin() -> IOHandle`, `stdout() -> IOHandle`, `stderr() -> IOHandle`.
- [ ] `write(handle, bytes : Bytes) -> Result<i32>`.
- [ ] `writeText(handle, text : string) -> Result<i32>`.
- [ ] `flush(handle) -> Result<void>`.
- [ ] No `print`/`println`; those belong only to `Standard.IO`.

### `System.FS`

- [ ] Types: `FileHandle`, `DirHandle`, `DirEntry`, `FileStat`, `OpenMode`, `SeekOrigin`.
- [ ] `open(path, mode) -> Result<FileHandle>`.
- [ ] `close(file) -> Result<void>`.
- [ ] `read(file, maxBytes) -> Result<Bytes>`.
- [ ] `write(file, data : Bytes) -> Result<i32>`.
- [ ] `flush(file) -> Result<void>`.
- [ ] `seek(file, offset, origin) -> Result<i64>`.
- [ ] `tell(file) -> Result<i64>`.
- [ ] `stat(path) -> Result<FileStat>`.
- [ ] `exists/isFile/isDir(path) -> Result<bool>`.
- [ ] `listDir(path) -> Result<DirHandle>`, `nextDirEntry(dir) -> Option<DirEntry>`, `closeDir(dir) -> Result<void>`.
- [ ] `mkdir/mkdirAll/remove/copy/rename(...) -> Result<void>`.
- [ ] `cwd() -> Result<string>`, `setCwd(path) -> Result<void>`.
- [ ] Capability tags split filesystem read/write/open/delete/change-directory behavior.

### `System.Path`

- [ ] `separator() -> string`, `delimiter() -> string`.
- [ ] `isAbsolute(path) -> bool`.
- [ ] `normalize(path) -> string`.
- [ ] `absolute(path) -> Result<string>`.
- [ ] `relative(from, to) -> Result<string>`.
- [ ] `join(a, b) -> string`.
- [ ] `dirname/basename/ext/stem(path) -> string`.
- [ ] Keep filesystem existence checks in `System.FS`, not `System.Path`.

### `System.Env` and `System.OS`

- [ ] `System.Env.argsCount() -> i32`, `arg(index) -> Option<string>`.
- [ ] `System.Env.get(name) -> Option<string>`.
- [ ] `System.Env.set(name, value) -> Result<void>`.
- [ ] `System.Env.unset(name) -> Result<void>`.
- [ ] `System.Env.exePath() -> Result<string>`.
- [ ] `System.OS.platform() -> string`, `arch() -> string`.
- [ ] `System.OS.isLinux/isMacos/isWindows() -> bool`.
- [ ] `System.OS.pid() -> i32`, `cpuCount() -> i32`, `pageSize() -> i32`.
- [ ] `System.OS.exit(code) -> void`, `sleepMs(ms) -> void`.
- [ ] Remove old Env `platform/arch`; update fixtures and registry tests accordingly.

### `System.Time`

- [ ] `monoNs() -> i64`, `wallNs() -> i64`.
- [ ] `sleepNs(ns) -> void`, `sleepMs(ms) -> void`.
- [ ] Types: `TimerHandle`.
- [ ] `timerStart(ns) -> Result<TimerHandle>`.
- [ ] `timerCancel(timer) -> Result<void>`.
- [ ] Formatting belongs only to `Standard.Time`.

### `System.FFI`

- [ ] Types: `LibraryHandle`, `SymbolHandle`.
- [ ] `supported() -> bool`.
- [ ] `open(path) -> Result<LibraryHandle>`.
- [ ] `open(path, manifest : namespace) -> Result<LibraryHandle>`.
- [ ] `symbol(lib, name) -> Result<SymbolHandle>`.
- [ ] `close(lib) -> Result<void>`.
- [ ] `lastError() -> Option<string>`.
- [ ] Extern declarations remain required for typed calls.
- [ ] Remove `DL` terminology from diagnostics, TAST/ABI helpers, lowering, and runtime symbols except in legacy rejection messages.
- [ ] All dynamic calls pass canonical ABI validation; no library-specific call shims.
- [ ] Capability-gate FFI/dynamic loading and cleanup library handles on VM exit.

### `System.ASM`

- [ ] Types: `UnitHandle`, `ObjectHandle`, `SymbolHandle`, `Target`, `Options`, `LinkMode`.
- [ ] `fromC(source, options) -> Result<UnitHandle>`.
- [ ] `fromDynASM(source, options) -> Result<UnitHandle>`.
- [ ] `compile(unit, target) -> Result<ObjectHandle>`.
- [ ] `symbol(object, name) -> Result<SymbolHandle>`.
- [ ] `linkStub(object, mode) -> Result<void>`.
- [ ] `linkAot(object, mode) -> Result<void>`.
- [ ] `closeUnit(unit) -> Result<void>`, `closeObject(object) -> Result<void>`.
- [ ] Capability: `native-code-generation`.
- [ ] Build integration: manifest support, cache keys, diagnostics, stub embedding, AOT direct-link path.

### `System.Buffer` and `System.Bytes`

- [ ] `System.Buffer` is the low-level mutable/native/runtime buffer API.
- [ ] Types: `BufferHandle` or equivalent resource-backed mutable buffer handle where native lifetime/pinning/FFI semantics are required.
- [ ] `System.Buffer.alloc(size) -> Result<BufferHandle>`.
- [ ] `System.Buffer.free(buffer) -> Result<void>`.
- [ ] `System.Buffer.len(buffer) -> Result<i32>`.
- [ ] `System.Buffer.get(buffer, index) -> Result<u8>`.
- [ ] `System.Buffer.set(buffer, index, value : u8) -> Result<void>`.
- [ ] `System.Buffer.slice(buffer, start, end) -> Result<BufferHandle>` or documented view/copy semantics.
- [ ] `System.Buffer.copy(dst, dstOffset, src, srcOffset, count) -> Result<i32>`.
- [ ] Endian accessors: `readU16LE/readU32LE/readU64LE`, `writeU16LE/writeU32LE/writeU64LE`.
- [ ] Native/FFI behavior is explicit: ownership, cleanup, pinning/moving, borrowed views, and pointer exposure policy.
- [ ] `System.Bytes` is the low-level immutable/owned byte-value API when the language/runtime has a distinct `Bytes` value type.
- [ ] `System.Bytes.new(size) -> Result<Bytes>`, `len`, `get`, `slice`, `copy` for value-level byte data.
- [ ] Do not expose duplicate APIs: mutable/resource/cursor behavior goes to `System.Buffer`; immutable value behavior goes to `System.Bytes`.
- [ ] Bounds diagnostics for all operations.

### `System.Json`

- [ ] Type: `JsonHandle` with cleanup on VM exit or explicit `free`.
- [ ] `parse(text) -> Result<JsonHandle>`.
- [ ] `free(json) -> Result<void>`.
- [ ] `stringify(json) -> Result<string>`.
- [ ] `kind(json) -> Result<i32>`.
- [ ] `get(json, key) -> Result<JsonHandle>`.
- [ ] `at(json, index) -> Result<JsonHandle>`.
- [ ] `len(json) -> Result<i32>`.
- [ ] `asString/asI64/asF64/asBool(json) -> Result<T>`.

### `System.Log`

- [ ] `log(level, message) -> void`.
- [ ] `setLevel(level) -> void`.
- [ ] `setFile(path) -> Result<void>`.
- [ ] `flush() -> Result<void>`.
- [ ] Move `debug/info/warn/error` convenience wrappers to `Standard.Log` only.

### `System.Random`

- [ ] `seed(seed : i64) -> void`.
- [ ] `i32() -> i32`, `i64() -> i64`, `f64() -> f64`.
- [ ] `fillBytes(bytes : Bytes) -> Result<void>`.
- [ ] Move `range/bool/bytes(count)` helpers to `Standard.Random` only.

### `System.Thread`, `System.Job`, and `System.Channel`

- [ ] `System.Thread.ThreadHandle`; keep OS/runtime thread handles distinct from VM jobs.
- [ ] `System.Thread.yield()`, `sleepMs(ms)`, `hardwareConcurrency()`.
- [ ] `System.Thread.spawn(fn void ()) -> Result<ThreadHandle>` only after closure/rooting is correct.
- [ ] `System.Thread.join/detach(thread) -> Result<void>`.
- [ ] `System.Job.JobHandle`, `Promise<T>`.
- [ ] `System.Job.spawn<T>(fn T ()) -> Result<Promise<T>>`.
- [ ] `System.Job.cancel/poll/await<T>(promise)` with `Result`/`Option`.
- [ ] `System.Channel.ChannelHandle` concrete families for `I32`, `I64`, `F32`, `F64`, `Bool`, `String`, `Bytes`.
- [ ] Channel APIs: `newT`, `sendT`, `trySendT`, `recvT`, `tryRecvT`, `pendingT`, `close` with final `Result`/`Option` shapes.
- [ ] Generic `Channel<T>` waits for generic/runtime support.
- [ ] close/cancel wakes blocked operations and propagates structured errors.

### `System.Process`

- [ ] Types: `ProcessHandle`, `ProcessOptions`, `ProcessStatus`.
- [ ] `spawn(path, args, options) -> Result<ProcessHandle>`.
- [ ] `wait(process) -> Result<ProcessStatus>`.
- [ ] `kill(process) -> Result<void>`.
- [ ] `stdin/stdout/stderr(process) -> Result<System.IO.IOHandle>`.
- [ ] Capability: `process-spawn`.

### `System.Net`

- [ ] Types: `SocketHandle`, `ListenerHandle`, `Address`.
- [ ] TCP: `tcpConnect`, `tcpListen`, `accept`, `send`, `recv`, `close`.
- [ ] UDP: `udpOpen`, `udpSendTo`, `udpRecvFrom`.
- [ ] Capability tags: `network-client`, `network-server`.
- [ ] Platform error normalization, timeout/non-blocking plan, and resource cleanup tests.

### `System.HTTP`

- [ ] Types: `RequestHandle`, `ResponseHandle`, `ServerHandle`, `TlsConfigHandle`.
- [ ] Client: `clientRequest`, `setHeader`, `writeBody`, `send`, `responseStatus`, `responseBody`, `closeResponse`.
- [ ] Server: `listenHttp`, `listenHttps`, `accept`, `writeResponse`, `closeServer`.
- [ ] Robustness: header limits, body limits, timeouts, backpressure, graceful shutdown, TLS verification/config diagnostics.
- [ ] Parser fuzz/regression tests.

### `System.Terminal`

- [ ] Types: `TerminalHandle`, `TerminalEvent`, `TerminalSize`.
- [ ] `open/close`.
- [ ] raw mode and alternate screen enter/exit.
- [ ] size, clear, clearLine.
- [ ] cursor move/show/hide.
- [ ] write/writeAt/flush.
- [ ] pollEvent/readEvent.
- [ ] Restore terminal mode on VM exit, trap, and Ctrl-C where possible.
- [ ] Windows terminal support.

### `System.Capability`, `System.Runtime`, and `System.Debug`

- [ ] `System.Capability.has/require/deny(name)`.
- [ ] Capability policy covers filesystem, environment, process args, FFI/dynamic loading, native code generation, threading, clock/time, randomness, network client/server, terminal control, and process spawn.
- [ ] `System.Runtime.version/gcCollect/gcStats/heapStats/jitEnabled/jitStats` with debug/capability gating where appropriate.
- [ ] `System.Debug.trap/assert/stackTrace/breakpoint` with debug/capability gating where appropriate.

---

## Phase 5: High-Level `Standard.*` APIs

`Standard.*` is ergonomic, source-level where possible, and built over `System.*`. It never bypasses `System.*` ownership, capability, blocking, allocation, or cleanup rules. The final public model has no top-level short aliases and no duplicate Standard roots that expose raw System members.

Required `Standard.*` modules:

```txt
Standard.IO
Standard.Console
Standard.FS
Standard.Path
Standard.Buffer
Standard.Bytes
Standard.Text
Standard.Json
Standard.Math
Standard.Random
Standard.Time
Standard.Log
Standard.Process
Standard.Net
Standard.HTTP
Standard.HTTPS
Standard.Terminal
Standard.Promise
Standard.Channel
Standard.Collections
Standard.Result
Standard.Option
```

Rules:

- [ ] High-level APIs wrap documented `System.*` APIs; each API doc lists `wraps System.X.y`, allocation behavior, blocking behavior, `Result`/`Option` shape, cleanup behavior, examples, and tests.
- [ ] Prefer source-level Simple wrappers once language features support them.
- [ ] Low-level APIs stay under `System.*`; convenience, formatting, composition, safety wrappers, and user-facing ergonomics stay under `Standard.*`.
- [ ] Generated docs show canonical `System.*` and `Standard.*` names only.
- [ ] Async helpers use lowercase `.async`, e.g. `Standard.HTTP.async.get`, `Standard.FS.async.readText`.

### `Standard.IO` and `Standard.Console`

- [ ] `Standard.IO.print/println(value)` and format variants.
- [ ] `Standard.IO.readLine() -> Result<string>`.
- [ ] `Standard.IO` wraps `System.IO`; no low-level handle APIs exposed.
- [ ] `Standard.Console.write/writeLine/readLine/clear`.
- [ ] `Standard.Console.setColor/resetColor`.
- [ ] Console wraps `System.IO` and `System.Terminal` and restores terminal state where applicable.

### Files, paths, buffers, bytes, and text

- [ ] `Standard.FS.readText/writeText/appendText -> Result<T>`.
- [ ] `Standard.FS.readBytes/writeBytes -> Result<T>`.
- [ ] `Standard.FS.copy/move/remove/ensureDir/list/walk -> Result<T>`.
- [ ] `Standard.FS.async.*` variants returning `Promise<T>` after Promise support.
- [ ] `Standard.Path.join(parts...)`, `dirname`, `basename`, `ext`, `stem`, `normalize`, `absolute`, `relative`.
- [ ] `Standard.Buffer` is the high-level mutable/growable/cursor buffer API for builders, readers, writers, and binary IO composition.
- [ ] `Standard.Buffer.new`, `withCapacity`, `len`, `capacity`, `clear`, `writeBytes`, `writeString`, `writeU16LE/writeU32LE/writeU64LE`, `readU*`, `toBytes`, and `fromBytes`.
- [ ] `Standard.Bytes` is the high-level immutable byte-value helper module: `new/fromString/toString/concat/slice/toHex/fromHex/toBase64/fromBase64`.
- [ ] `Standard.Text.len/isEmpty/contains/startsWith/endsWith/trim/split/join/replace`.

### JSON, math, random, time, and log

- [ ] `Standard.Json` remains unavailable until a real `JsonValue` API or deliberate thin handle wrapper is implemented.
- [ ] Target `Standard.Json`: `parse/stringify/get/at/asString/asI64/asF64/asBool` over `JsonValue`.
- [ ] `Standard.Math.PI`, `abs`, `min`, `max`, `sqrt`, `clamp`, `lerp`.
- [ ] `Standard.Random.seed/i32/i64/range/f64/bool/bytes`.
- [ ] `Standard.Time.monoNs/nowNs/sleepMs/formatWallNs` plus future `Duration` and `Instant` helpers.
- [ ] `Standard.Log.debug/info/warn/error/setLevel/setFile`.

### Process, network, HTTP, HTTPS, and terminal

- [ ] `Standard.Process.ProcessResult`, `run`, `runText`, `async.run`.
- [ ] `Standard.Net.TcpStream/TcpListener`, `connect`, `listen`, `read`, `write`, `close`.
- [ ] `Standard.HTTP.HTTPRequest/HTTPResponse/HTTPServer`, `get/post/put/delete`, `async.get/post`, `serve`.
- [ ] `Standard.HTTPS.TlsOptions`, `get/post`, secure `serve`; secure defaults documented.
- [ ] `Standard.Terminal.Terminal`, `open/close`, `withRaw`, `withAltScreen`, `clear`, `size`, `moveCursor`, `writeAt`, `readEvent`, `pollEvent`.
- [ ] Terminal helpers guarantee restoration where possible.

### Promise, channels, collections, result, and option

- [ ] `Standard.Promise.run/await/poll/cancel/isDone` wrapping `System.Job`.
- [ ] `Standard.Channel.Channel<T>` generic wrapper after generic/runtime support lands.
- [ ] `Standard.Collections.List/Map/Set/Queue/Stack` as source-level generic code where possible.
- [ ] `Standard.Result.ok/err/isOk/unwrap` helpers around language `Result<T,E>`.
- [ ] `Standard.Option.some/none/isSome/unwrap` helpers around language `Option<T>`.

### Release hardening for System/Standard

- [ ] Migration docs updated: `Docs/System.md`, `Docs/Standard.md`, `Docs/LibraryMigration.md`, `Docs/Language.md`, README.
- [ ] Inventory maps every removed alias to final owner and every transitional runtime symbol to final canonical symbol or deletion.
- [ ] Native registry generated reference is canonical and complete.
- [ ] LSP expectations and editor snippets/completions are canonical.
- [ ] Playground, Website, and examples are canonical.
- [ ] Full cross-platform tests for Linux/macOS/Windows behavior where APIs touch host/runtime state.
- [ ] Version bump and changelog call out the breaking no-alias runtime naming change.

---

## Phase 5.5: Simple Editor and LSP Experience

Goal: make Simple pleasant to write in VS Code by wiring the existing `svm` pipeline into editor
commands, tasks, diagnostics, and navigation. The TypeC extension is a process reference for how to
package and execute tooling, but the feature set and commands must be Simple-specific and must map
to real Simple CLI/LSP capabilities.

Simple toolchain facts this plan must respect:

- `svm lsp` starts the stdio LSP server.
- `svm check <file.simple|file.sir|file.sbc>` validates without running.
- `svm run <file.simple|file.sir|file.sbc> [-jit|-int] [--jit-stats] [--no-verify]` runs programs.
- `svm build|compile <file.simple|file.sir> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]` builds artifacts.
- `svm emit -ir <file.simple> [--out <file.sir>]` emits SIR.
- `svm emit -sbc <file.sir|file.simple> [--out <file.sbc>] [--no-verify]` emits bytecode.
- `simple` is runtime-stub only and must not be used for editor compiler commands.

### VS Code Build and Packaging

- [x] Convert `Editor/vscode-simple` from hand-written JavaScript to a TypeScript extension build.
  - [x] Add `src/extension.ts`, `tsconfig.json`, typed command constants, and generated `out/extension.js`.
  - [x] Add `compile`, `watch`, `lint`, `package`, and prepublish scripts.
  - [x] Add `@types/vscode`, `typescript`, and `@vscode/vsce` dev dependencies.
  - [x] Keep checked-in generated output only if VSIX packaging/release requires it.
- [x] Keep VSIX version synchronized with `VERSION`, but do not let extension packaging change the `svm` version.
- [x] Package VSIX in CI by running `npm ci`, extension validation, and `vsce package`.
- [x] Support `svm` discovery in this order:
  - [x] explicit `simple.compilerPath` setting.
  - [x] bundled release binary under the extension/package layout when present.
  - [x] `svm` on `PATH`.
  - [x] No workspace-local compiler path probing in the distributed extension.
- [x] Show a clear error with settings shortcut when no usable `svm` is found.
- [x] Add extension smoke validation for command registration and package manifest validity.
- [ ] Add extension smoke tests for activation and configured path resolution.

### Simple VS Code Commands and Tasks

- [x] Add VS Code commands that execute real `svm` commands:
  - [x] `Simple: Check Current File` -> `svm check <file>`.
  - [x] `Simple: Run Current File` -> `svm run <file>`.
  - [x] `Simple: Run Current File With JIT` -> `svm run <file> -jit --jit-stats`.
  - [x] `Simple: Build Current File` -> `svm build <file>`.
  - [x] `Simple: Compile Current File` -> `svm compile <file>`.
  - [x] `Simple: Emit SIR` -> `svm emit -ir <file> --out <file.sir>`.
  - [x] `Simple: Emit SBC` -> `svm emit -sbc <file> --out <file.sbc>`.
  - [x] `Simple: Restart Language Server` -> stop/start `svm lsp`.
  - [x] `Simple: Show Language Server Output`.
  - [x] `Simple: Show svm Version` -> `svm version` task.
  - [x] `Simple: Show svm Help` -> `svm help` task.
  - [x] `Simple: Configure Compiler Path` settings shortcut.
  - [x] Add build output directory and JIT default settings commands.
  - [x] Add trace settings command.
- [x] Add command palette, editor title, and editor context entries for `.simple` files.
- [x] Add explorer context entries for `.simple` files.
- [x] Add explorer context entries for `.sir` and `.sbc` where appropriate.
- [x] Add Simple task provider:
  - [x] default build task for active `.simple` file.
  - [x] run/check/emit tasks for active file.
  - [x] optional problem matcher for `path:line:column: message` diagnostics emitted by `svm`.
- [x] Route all command tasks through `svm` with document-directory cwd and module-style `.simple` inputs.
- [x] Add optional configured output directory.
- [x] Never invoke the `simple` runtime stub for check/build/emit/lsp.

### Simple LSP Feature Roadmap

Current Simple LSP already has diagnostics, hover, completion, signature help, definition,
declaration, references, document highlights, rename/prepare-rename, code actions, document
symbols, workspace symbols, and semantic tokens.

Correctness-first LSP basics before new capability work:

- [x] Hover uses canonical Simple syntax for mutable/immutable variables, parameters, function signatures, module/import declarations, artifacts, artifact fields, enums, and enum members.
- [x] Hover regression covers duplicate function/immutable variable names without inventing mutable information.
- [x] Semantic tokens distinguish mutable/immutable declarations, parameters, functions, namespaces/modules, artifacts, artifact fields, enums, and enum members with exact token types/modifiers.
- [x] Inlay hints use only valid Simple syntax and never invent unknown parameter/type facts.
- [x] Signature help uses canonical Simple function syntax for Simple and reserved/native functions.
- [x] Namespace/member functions use canonical qualified names in hover, signature help, semantic tokens, and inlay hints.

Next Simple-specific LSP capabilities:

- [x] Full and range formatting backed by a Simple indentation formatter for `.simple` source.
- [x] Type-definition lookup for artifact/enum type declarations and typed variable usages.
- [x] Type-definition lookup for generic/handle type arguments.
- [ ] Type-definition lookup for native ABI type uses.
- [x] Linked editing ranges for same-document Simple identifier declarations/usages.
- [x] Call hierarchy prepare/incoming/outgoing for Simple top-level functions across opened documents.
- [x] Call hierarchy for namespace/member functions.
- [x] Call hierarchy across indexed imported/workspace functions.
- [ ] Call hierarchy for native wrappers.
- [x] Folding ranges for brace-delimited Simple regions.
- [x] Selection ranges for token -> enclosing brace-region nesting.
- [x] Inlay hints for Simple function-call parameter names.
- [x] Inlay hints for function result types.
- [ ] Inlay hints for inferred locals, generic instantiations, ABI/native-call effects, and optional JIT-safety facts.
- [x] Document links for local `import` paths.
- [x] Document links for module-header imports and known standard/native docs.
- [x] Document links for module-map entries.
- [ ] Document links for generated SIR/SBC outputs.
- [x] Code lenses for check/run/JIT-run commands on top-level Simple functions and entrypoints.
- [x] Code lenses for tests/examples.
- [ ] Code lenses for exported functions and persisted JIT stats.
- [x] Code actions for unknown identifiers and quick emit/build actions.
- [x] Code actions for missing import files.
- [ ] Code actions for unresolved module imports and signature mismatch fixes.
- [x] Workspace symbols and references across opened documents plus sibling indexed `.simple` files.
- [x] Workspace symbols across recursive opened-document project directories.
- [x] Workspace symbols and references include files reached through `simple.modules` module maps.
- [ ] Workspace symbols and references across full project roots.
- [x] Range-based document sync with incremental text changes tested across open/change/close.

### Simple LSP Architecture

- [ ] Split `LSP/src/lsp_server.cpp` into Simple feature modules:
  JSON-RPC/framing, document store, diagnostics, formatting, symbols, semantic tokens,
  completion, hover, signature help, inlay hints, document links, folding, selection ranges,
  call hierarchy, code actions, code lenses, and workspace index.
- [x] Add opened-document version tracking and incremental range-change application.
- [ ] Build a reusable opened-document store with URI, version, language id, text, and dirty flag.
- [x] Build an initial Simple workspace index over opened document directories and recursive `.simple` files.
- [x] Extend the workspace index over Simple module-map paths.
- [ ] Extend the workspace index over `.sir`, `.sbc`, import paths, declarations, exported symbols, type IDs, and native metadata.
- [ ] Cache parse/RAST/TAST/SIR facts per document with invalidation on text/file/import changes.
- [ ] Reuse the compiler pipeline exactly for diagnostics; LSP should only map spans/ranges and shape JSON-RPC responses.
- [x] Advertise folding ranges, selection ranges, and document links only after protocol-level tests cover initialize + requests.
- [ ] Advertise remaining LSP capabilities only after protocol-level tests cover initialize + request + edge cases.
- [ ] Keep `svm check` and LSP diagnostics consistent by sharing diagnostic codes, messages, and span mapping.

---

## Phase 6: Documentation and Tests

- [ ] Generate native API docs grouped by layer with canonical `System.*` / `Standard.*` names only.
  - [x] Generated native metadata docs include module/symbol/signature rows.
- [x] Generate capability table in native metadata docs.
- [x] Generate resource-use table in native metadata docs.
- [x] Generate platform availability table in native metadata docs.
- [x] Test every public native function has valid metadata.
- [ ] Test generated docs, LSP completions, snippets, README, examples, playgrounds, Website samples, and fixtures contain no lowercase runtime modules or legacy aliases outside explicit rejection examples.
- [ ] Test native registry rejects lowercase module names and short internal module identifiers.
- [ ] Test every resource-producing function declares resource kind and cleanup behavior.
  - [x] File and FFI library metadata tests cover resource outputs/inputs, ownership, and cleanup behavior.
- [x] Unit tests for resource registry.
- [x] VM tests for handle lifecycle edge cases.
- [ ] Native family tests:
  - [ ] FS/path.
  - [ ] buffer/bytes.
  - [ ] Json.
  - [ ] channel/thread/job/promise.
  - [ ] FFI/ASM.
  - [ ] env/os/time/random/log.
  - [ ] net/HTTP/HTTPS/process/terminal.
- [ ] CLI integration tests through `svm`.
- [ ] Cross-platform path/env/process/terminal tests.
- [ ] Stress tests:
  - [ ] many open/close cycles.
  - [ ] VM shutdown with many live handles.
  - [ ] concurrent channel/job cleanup.
  - [ ] large file/buffer operations.

---

# Lower-Priority Backlog

This backlog is intentionally below native-library prerequisites and native-library feature work. Do not prioritize broad SRP-only cleanup over native work. Perform these when they directly support native work, fix a bug, or keep touched code maintainable.

## SRP / Module Boundary Debt

- [ ] `VM/src/vm.cpp` is orchestration/API only.
  - Current status: not closed; `ExecuteModule` still owns the main opcode switch and substantial instruction behavior.
  - Continue only when opcode/runtime changes are already being touched.
- [ ] `Lang/src/lang_validate.cpp` is not a semantic monolith.
  - Current status: not closed; it still owns broad `InferExprType`, call checking, statement checking, type checking, and validation context behavior.
  - Continue only when semantic/native API work already touches validation.
- [x] Native runtime additions use metadata registry and include metadata/signature tests; no ad-hoc native dispatch lists or forwarding glue are reintroduced.
- [x] Current GC tracing uses declared roots/stack maps/globals rather than heuristic ref guessing.
- [x] Lang phase APIs are direct owner APIs, not facade-only wrappers or compatibility shims.
- [ ] Keep this timeline actionable with file-level tasks.

## Packaging / Distribution

- [ ] Add `svm package` after core runtime/library work stabilizes.
- [ ] Current-platform package first; multi-target package assembly later.
- [ ] Package embedded-SBC stubs, assets, FFI libraries, and developer-provided ASM/AOT native objects without trying to manage application-specific FFI policy.

## Lang Architecture Follow-Up

- [ ] Finish replacing facade-only Lang split with real implementation modules where still incomplete.
- [ ] Persist expression types, mutability facts, and ABI facts on typed nodes.
- [ ] Replace repeated string-based semantic lookup with resolved IDs where useful.
- [ ] Keep test split inactive unless it directly supports touched native/compiler work.

## AOT / JIT / Runtime Follow-Up

- [ ] AOT uses the same Simple Native ABI, resource registry, capability policy, and native metadata as interpreter/JIT.
- [ ] AOT consumes the same monomorphized concrete IR/SBC facts as LLVM ORC JIT.
- [ ] AOT emits or links the same root/safepoint metadata required by heap-aware JIT execution.
- [ ] AOT package/build work starts only after Phase 1 ABI and Phase 2.5 JIT helper ABI are stable.
