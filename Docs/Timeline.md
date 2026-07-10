# Simple Timeline

This is the canonical project roadmap. Native library prerequisites and native-library features take priority over older backlog items whenever priorities conflict.

Order of work:

1. Stabilize the current interpreter/CLI/test baseline.
2. Complete compiler / IR / bytecode prerequisites for language-neutral VM types and operations.
3. Complete the language surface: monomorphic generics, closures, `Result`/`Option`/`Promise`, and resource-safe control flow.
4. Native ABI and language-level result/option/promise/handle types.
5. VM Native Core resource registry and safety model.
6. LLVM ORC JIT broad Tier 0, then optimized Tier 1.
7. Low-level `System.*` APIs.
8. High-level `Standard.*` APIs and approved aliases.
9. Documentation, generated API references, and cross-platform tests.
10. Lower-priority SRP/backlog cleanup only when it directly supports the above.

Locked architecture decisions:

- The interpreter is the semantic baseline. The LLVM ORC JIT may only accelerate validated SBC and must fallback or trap safely when unsupported.
- SBC remains the portable artifact. JIT output is platform-local cacheable machine code, not a distribution format.
- `data` is stable-layout ABI data. `artifact` is a managed language object. Methods never affect layout.
- Generics are compile-time monomorphic. Every concrete instantiation is type-checked and emitted as a concrete specialized declaration.
- Generic arguments may be any type with stable type identity: primitives, strings, arrays, lists, data, artifacts, enums, pointers, functions, handles, results/options/promises, and instantiated generic types.
- Native calls are metadata-driven. Interpreter, JIT, future AOT, docs, and language reserved signatures must consume the same native-function metadata.
- Host resources are generational opaque handles. Raw platform handles and VM heap internals are never exposed directly to Simple code.
- `System.*` is explicit, low-level, capability-aware, and returns `Result`/`Option` for expected host failures. `Standard.*` is ergonomic and wraps `System.*`.

---

## Phase 0: Compiler / IR / Bytecode Prerequisites

These lower layers remain language-neutral. They define VM-level type metadata, bytecode operations, verification, and lowering only; language sugar and native library names are tracked in later phases.

### Type-System Prerequisites

- [x] Add generic VM metadata for result-like, option-like, vector, aggregate, function, and pointer types.
- [x] Keep artifact-as-struct layout explicit in SIR/SBC type metadata.
- [x] Keep artifact methods separate from artifact layout metadata.
- [ ] Add opaque handle/resource metadata without naming language or native-library modules.
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

### Bytecode / SBC Prerequisites

Language-neutral SBC metadata/opcodes status:

- [x] Type/metadata rows.
  - [x] result-like type row.
  - [x] option-like type row.
  - [ ] opaque handle/resource metadata.
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

- [ ] Define canonical type identity for every generic argument kind.
  - [ ] primitives.
  - [ ] `string`, `Bytes`, heap references, and managed artifacts.
  - [ ] arrays/lists and nested aggregate types.
  - [ ] stable-layout `data` types.
  - [ ] enums and pointer types.
  - [ ] function/procedure types including captured closures.
  - [ ] `System.Handle<T>`, `Result<T,E>`, `Option<T>`, `Promise<T>`, `Channel<T>`.
  - [ ] instantiated generic types as generic arguments.
- [ ] Define deterministic generic symbol mangling.
  - [ ] Human/debug form, e.g. `Map<string, List<i32>>`.
  - [ ] Link/internal form with stable escaping or hash suffixes.
  - [ ] Collision detection with diagnostic output.
- [ ] Add generic declaration metadata to TAST and SIR/SBC debug/type metadata.
- [ ] Reject recursive value containment without indirection; allow recursion through pointer/ref/handle.

### Specialization Pipeline

- [ ] Add `Lang/include/GEN/specializer.h`.
- [ ] Add `Lang/src/GEN/specializer.cpp`.
- [ ] Insert specialization after generic TAST validation and before IRB lowering.
- [ ] Collect generic function/data/artifact declarations.
- [ ] Collect concrete instantiation requests from call sites, type annotations, literals, fields, globals, imports, and native signatures.
- [ ] Instantiate dependencies recursively with cycle detection.
- [ ] Re-run semantic/type checks on each concrete specialization.
- [ ] Cache/reuse equivalent specializations across a module graph.
- [ ] Emit only concrete specialized declarations into IRB/IRE.

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
  - [x] Native handlers decode arguments through typed accessors, including packed handles and borrowed string/byte views.
  - [x] Native handlers build returns through typed builders.
  - [x] Resource, capability, allocation, blocking, and GC behavior are visible through metadata.
- [ ] Define restricted External C FFI ABI.
  - [ ] Permit primitives, pointers, stable `data` structs, and explicit ABI wrapper types.
  - [ ] Reject managed artifacts, closures, VM heap internals, and implicit `string -> char*` coercions.
  - [ ] Use libffi/platform ABI only after Simple ABI metadata and layout checks pass.
- [ ] Keep interpreter, LLVM ORC JIT, and future AOT on the same Simple Native ABI contract.

### Exact ABI Mapping Table

- [ ] Define exact ABI mapping table for every native-callable type.
  - [x] Add runtime primitive ABI classifier for current SBC `TypeKind` values.
- [ ] Scalar representations:
  - [x] signed/unsigned integers use exact-width two's-complement payloads.
  - [x] floats use IEEE-754 `f32`/`f64`.
  - [x] `bool` ABI is `u8` with only `0` or `1` valid.
  - [x] `char` ABI is `u32` Unicode scalar long-term; document current bytecode compatibility if still 16-bit internally.
  - [ ] enums use declared or default underlying integer type.
- [ ] Reference/handle representations:
  - [ ] VM heap references are opaque VM refs, never raw host pointers.
  - [x] `System.Handle<T>` lowers to `NativeHandleId` packed as a VM word.
  - [x] `Promise<T>` lowers to a generational promise/job id.

### Stable `data` Layout ABI

- [x] Field declaration order is layout order.
- [x] Alignment rules:
  - [x] 1-byte: `bool`, `i8`, `u8`.
  - [x] 2-byte: `i16`, `u16`.
  - [x] 4-byte: `i32`, `u32`, `f32`, `char`, enum32.
  - [x] 8-byte: `i64`, `u64`, `f64`, pointer, ref, handle.
  - [ ] nested `data`: max field alignment, capped at 8 initially.
- [ ] Padding is deterministic and zero-initialized where observable through ABI.
- [x] Total size rounds up to max alignment.
- [ ] Nested `data` structs are allowed after recursive layout validation.
- [ ] Recursive value containment is rejected; recursive pointer/ref/handle containment is allowed.
- [x] By-value internal passing for no-ref structs at or below the chosen small aggregate threshold, initially `<= 16` bytes.
- [x] Larger/ref-containing aggregates pass by readonly pointer or VM-managed reference according to metadata.
- [ ] Return-by-value rules match parameter rules and are explicit in metadata.
- [ ] Tests prove methods do not affect layout.
- [ ] Tests cover padding, nested data, arrays, generics, and layout hashes.
  - [x] Runtime ABI tests cover deterministic stable aggregate layout hashes.
  - [x] Runtime ABI tests cover primitive padding and ref-containing aggregate classification.

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
- [ ] External FFI requires explicit `cstring`, pointer, or bytes wrapper types; no implicit `string` coercion.

### Handles, Results, Options, Promises

- [x] Define `NativeHandleId { index, generation }` as the opaque VM payload for `System.Handle<T>`.
- [x] Native callees declare expected resource kind; metadata dispatch validates kind/generation/closed state before use.
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
- [ ] Add interpreter/JIT shared tests for the same native ABI calls.
- [ ] Document all mappings in `Docs/Language.md`, `Docs/VM.md`, `Docs/IR.md`, and `Docs/Byte.md`.

---

## Phase 2: VM Native Core

### Resource Registry

- [x] Add `VM/include/native/resource_registry.h`.
- [x] Add `VM/src/native/resource_registry.cpp`.
- [x] Define `NativeHandleId` as opaque VM value payload.
- [ ] Map each public `System.Handle<T>` to `NativeHandleId` plus resource kind/generation.
- [x] Define `NativeResourceKind`:
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
- [x] capability tags metadata shape.
- [x] platform availability metadata shape.
- [x] stability status metadata shape.
- [x] doc summary metadata shape and generated output.
- [x] metadata validator for required names, handlers, parameter types, resources, ownership, and cleanup.

### Capability Policy

- [ ] Add capability tags:
  - [x] filesystem read/write.
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

- [ ] Define `JitCallContext` as the stable internal ABI for compiled Simple functions.
  - [ ] argument access.
  - [ ] return slot.
  - [ ] locals/spill slots.
  - [ ] globals access.
  - [ ] heap/runtime helper access.
  - [ ] trap/error builder.
  - [ ] safepoint/root registration.
- [x] Define `JitStatus` result codes for halt, return, trap, fallback, and unsupported.
- [x] Aggregate JIT status counts in execution stats.
- [ ] Keep helper-heavy context ABI as the correctness path. Direct scalar native signatures are optimization-only.
- [ ] Cache ORC entries by module id, function id, code hash, ABI version, and runtime helper ABI version.

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
- [ ] Keep exact root facts for args, locals, operand stack values, captured refs, and temporary helper values.
- [ ] Add safepoint metadata to lowered call sites and loop backedges.
- [ ] Add tests that force GC from JIT helper calls.
- [ ] Later: replace coarse root slots with LLVM stackmap/root-map integration.

### Native Calls from JIT

- [ ] JIT native calls use `NativeFunctionSpec` id and the same dispatcher as the interpreter.
- [ ] Capability checks run identically in interpreter and JIT.
- [x] Resource kind/generation/closed-state checks run through shared metadata dispatch before native handlers.
- [ ] Blocking/native calls publish safepoints and roots before entering host code.
- [ ] Direct native binding is allowed only for pure, non-blocking, non-allocating, no-resource helpers after metadata marks them safe.

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
- [ ] Every JIT fallback reason is counted and optionally printed by `--jit-stats`.
- [ ] Runtime traps include opcode/function/pc/source debug context when available.
- [ ] Test `-jit` and `-int` produce identical user-visible results for fixture suites.
- [x] Remove old compiled-runner code; JIT work targets LLVM ORC plus interpreter fallback only.

---

## Phase 3: Low-Level `System.*` APIs

`System.*` is explicit and close to host/runtime behavior. It exposes typed handles, explicit options, explicit cleanup, clear blocking behavior, and `Result<T>` / `Option<T>` for expected failures/absence.

Sync/async convention:

- Sync APIs: `System.Domain.function(...) -> Result<T>` / `Option<T>`.
- Async APIs: `System.Domain.async.function(...) -> Promise<T>`.
- Do not use `Async` suffixes or capital `Async` modules.

### `System.FS`

- [ ] `System.FS.open(path, mode) -> Result<System.FS.FileHandle>`.
- [ ] `System.FS.close(file) -> Result<void>`.
- [ ] `System.FS.read(file, maxBytes) -> Result<Bytes>`.
- [ ] `System.FS.readAll(file) -> Result<Bytes>`.
- [ ] `System.FS.write(file, data) -> Result<i32>`.
- [ ] `System.FS.flush(file) -> Result<void>`.
- [ ] `System.FS.seek(file, offset, origin) -> Result<i64>`.
- [ ] `System.FS.tell(file) -> Result<i64>`.
- [ ] `System.FS.stat(path) -> Result<System.FS.FileStat>`.
- [ ] `System.FS.exists/isFile/isDir(path) -> Result<bool>`.
- [ ] `System.FS.listDir(path) -> Result<List<string>>`.
- [ ] `System.FS.mkdir/mkdirAll/remove/copy/rename(...) -> Result<void>`.
- [ ] `System.FS.cwd() -> Result<string>`.
- [ ] `System.FS.setCwd(path) -> Result<void>`.
- [ ] Async variants under `System.FS.async.*`.

### `System.Path`

- [ ] join, joinMany, dirname, basename, ext, stem.
- [ ] normalize, absolute, relative, isAbsolute, separator.

### `System.Buffer` / `System.Bytes`

- [ ] Decide VM heap bytes vs native buffer handles or both.
- [ ] buffer create/close/len/slice/copy APIs.
- [ ] endian read/write APIs.
- [ ] bounds diagnostics for all operations.

### `System.FFI`

Foreign function interface and dynamic loading. Unsafe/system-level by default.

- [ ] `System.FFI.open(path) -> Result<System.FFI.LibraryHandle>`.
- [ ] `System.FFI.close(lib) -> Result<void>`.
- [ ] `System.FFI.symbol(lib, name) -> Result<System.FFI.SymbolHandle>`.
- [ ] `System.FFI.error() -> Option<string>`.
- [ ] Extern declarations remain the preferred way to call typed FFI symbols.
- [ ] Capability-gate FFI/dynamic loading.
- [ ] Cleanup library handles on VM exit.

### `System.ASM`

Native code-generation tooling for C/DynASM source units. Goal: compile/link native code into the generated stub or AOT output without shipping ad-hoc app-specific dynamic libraries.

- [ ] Handles/types:
  - [ ] `System.ASM.UnitHandle`.
  - [ ] `System.ASM.ObjectHandle`.
  - [ ] `System.ASM.SymbolHandle`.
  - [ ] `System.ASM.Target`.
  - [ ] `System.ASM.Options`.
  - [ ] `System.ASM.LinkMode`.
- [ ] APIs:
  - [ ] `System.ASM.fromC(source, options) -> Result<UnitHandle>`.
  - [ ] `System.ASM.fromDynASM(source, options) -> Result<UnitHandle>`.
  - [ ] `System.ASM.compile(unit, target) -> Result<ObjectHandle>`.
  - [ ] `System.ASM.symbol(object, name) -> Result<SymbolHandle>`.
  - [ ] `System.ASM.linkStub(object, mode) -> Result<void>`.
  - [ ] `System.ASM.linkAot(object, mode) -> Result<void>`.
  - [ ] close unit/object APIs.
- [ ] Build integration:
  - [ ] `svm build` manifest support for C/DynASM units.
  - [ ] symbol metadata tying Simple extern declarations to ASM-produced symbols.
  - [ ] stub embedding path.
  - [ ] AOT direct-link path.
  - [ ] cache keys for source, target, options, ABI metadata, compiler version.
  - [ ] diagnostics for missing compiler/toolchain/DynASM backend.
- [ ] Safety:
  - [ ] explicit native-code-generation capability.
  - [ ] no implicit raw pointer/resource access.
  - [ ] ABI signatures required before Simple calls generated code.

### `System.Thread` / `System.Job` / `System.Channel`

- [ ] Keep OS thread handles distinct from VM job handles.
- [ ] thread sleep/yield/hardwareConcurrency.
- [ ] job spawn/join/detach/cancel.
- [ ] concrete channel variants until generic runtime support lands.
- [ ] channel send/trySend/recv/tryRecv/pending/close.
- [ ] close/cancel wakes blocked operations.
- [ ] structured error propagation through `Result<T>`.

### `System.Net`

- [ ] TCP client socket handle.
- [ ] TCP listener handle.
- [ ] UDP socket handle.
- [ ] connect/listen/accept/send/recv/close returning `Result<T>`.
- [ ] non-blocking/timeout variants.
- [ ] platform error normalization.

### `System.HTTP`

- [ ] HTTP client request API returning `Result<HTTPResponse>`.
- [ ] HTTPS client TLS verification options.
- [ ] HTTP server handles:
  - [ ] listenHttp.
  - [ ] accept.
  - [ ] readRequest/readBody.
  - [ ] writeResponse.
  - [ ] closeConnection/closeServer.
- [ ] HTTPS server handles:
  - [ ] loadTlsConfig.
  - [ ] listenHttps.
  - [ ] certificate/config diagnostics.
- [ ] server robustness:
  - [ ] request/header size limits.
  - [ ] keep-alive policy.
  - [ ] timeout policy.
  - [ ] backpressure.
  - [ ] graceful shutdown.
  - [ ] parser fuzz/regression tests.

### `System.Terminal`

Low-level terminal/console control; enough to build TUIs/games in user code, not a built-in widget/game framework.

- [ ] terminal open/close.
- [ ] raw mode enter/exit.
- [ ] alternate screen enter/exit.
- [ ] mouse enable/disable.
- [ ] capabilities.
- [ ] size, clear, clearLine.
- [ ] cursor move/show/hide.
- [ ] set title.
- [ ] write/writeAt/style/reset/flush.
- [ ] pollEvent/readEvent.
- [ ] key, mouse, resize event values.
- [ ] guaranteed terminal mode restoration on VM exit/trap.
- [ ] Windows terminal support.

### `System.Process` / `System.Time` / `System.Env` / `System.OS` / `System.Json` / `System.Log` / `System.Random`

- [ ] Process spawn/wait/kill/stdin/stdout/stderr with capability policy.
- [ ] Time wall/mono clocks and timer handles.
- [ ] Env args/get/set with capability policy.
- [ ] OS platform/arch/exePath.
- [ ] Json handle-based parse/stringify/accessors with VM-exit cleanup.
- [ ] Log levels/sinks/file sink through resource registry.
- [ ] Random seeded RNG handles and process-global convenience later.

---

## Phase 4: High-Level `Standard.*` APIs and Aliases

Canonical high-level modules live under `Standard.*`. Approved aliases expose ergonomic top-level domains such as `FS`, `Path`, `Bytes`, `Json`, `HTTP`, `HTTPS`, `Net`, `Process`, `Console`, `Terminal`, `Random`, `Time`, and `Log`.

Rules:

- [ ] High-level APIs wrap `System.*`; they do not bypass resource ownership.
- [ ] High-level aliases preserve lowercase `.async`, e.g. `HTTP.async.get`, `FS.async.readText`.
- [ ] Top-level aliases are high-level only; low-level APIs stay under `System.*`.
- [ ] Generated docs show both canonical names and aliases.

### Files / Paths / Bytes

- [ ] `FS.readText/writeText/appendText -> Result<T>`.
- [ ] `FS.readBytes/writeBytes -> Result<T>`.
- [ ] `FS.copy/move/remove/ensureDir/list/walk -> Result<T>`.
- [ ] async variants under `FS.async.*` returning `Promise<T>`.
- [ ] `Path` wrappers over `System.Path`.
- [x] `Bytes` heap-owned byte sequence; UTF-8 conversion and hex/base64 later.

### JSON

- [ ] `Json.parse(text) -> Result<JsonValue>`.
- [ ] `Json.stringify(value) -> Result<string>`.
- [ ] object/array builders.
- [ ] typed getters with defaults.

### Promise / Concurrency

- [ ] `Promise.run(fn) -> Promise<T>`.
- [ ] `Promise<T>.await() -> Result<T>`.
- [ ] `Promise<T>.poll() -> Option<Result<T>>`.
- [ ] `Promise<T>.cancel() -> Result<void>`.
- [ ] `Promise<T>.isDone() -> bool`.
- [ ] `Channel<T>` once runtime generic support exists.

### Net / HTTP / HTTPS

- [ ] `HTTP.get/post/put/delete -> Result<HTTPResponse>`.
- [ ] `HTTP.async.get/post/put/delete -> Promise<HTTPResponse>`.
- [ ] HTTP server helpers:
  - [ ] `HTTP.serve(host, port, handler) -> Result<HTTPServer>`.
  - [ ] request/response helpers: text, bytes, json, file.
  - [ ] graceful shutdown.
- [ ] HTTPS server helpers:
  - [ ] `HTTPS.serve(host, port, certPath, keyPath, handler) -> Result<HTTPSServer>`.
  - [ ] TLS options wrapper over `System.HTTP.TlsOptions`.
  - [ ] secure defaults documented.

### Console / Terminal

- [ ] `Console.write/writeLine/readLine/clear`.
- [ ] color helpers.
- [ ] `Console.keyAvailable/readKey`.
- [ ] `Terminal.size/clear/moveCursor/showCursor/hideCursor`.
- [ ] raw mode / alternate screen helpers.
- [ ] non-blocking event pump.
- [ ] optional frame-buffer/cell helpers.
- [ ] cleanup on panic/trap/Ctrl-C.

### Process / Time / Random / Log

- [ ] `Process.run(path, args) -> Result<ProcessResult>`.
- [ ] async process run under `Process.async.run`.
- [ ] `Time.now/mono/sleep` and duration type later.
- [ ] `Random.i32/range/f64`, seeded RNG later.
- [ ] top-level logging wrappers and structured logger later.

---

## Phase 5: Documentation and Tests

- [ ] Generate native API docs grouped by layer.
  - [x] Generated native metadata docs include module/symbol/signature rows.
- [x] Generate capability table in native metadata docs.
- [x] Generate resource-use table in native metadata docs.
- [x] Generate platform availability table in native metadata docs.
- [x] Test every public native function has valid metadata.
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
