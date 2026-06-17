# Simple Timeline

This is the canonical project roadmap. Native library prerequisites and native-library features take priority over older backlog items whenever priorities conflict.

Order of work:

1. Compiler / IR / bytecode prerequisites for native-facing types.
2. Native ABI and built-in result/option/promise/handle types.
3. VM Native Core resource registry and safety model.
4. Low-level `System.*` APIs.
5. High-level `Standard.*` APIs and approved aliases.
6. Documentation, generated API references, and cross-platform tests.
7. Lower-priority SRP/backlog cleanup only when it directly supports the above.

---

## Phase 0: Compiler / IR / Bytecode Prerequisites

The native library cannot be robust until these lower layers can represent the types and operations the library needs.

### Type-System Prerequisites

- [ ] Add built-in generic `System.Handle<T>`.
  - [ ] Preserve resource marker type `T` through validation, SIR, SBC metadata, and native dispatch.
  - [ ] Reject forging handles from plain integers in user code.
  - [ ] Preserve handle resource kind in diagnostics and generated docs.
- [ ] Add built-in `Result<T>`.
  - [ ] Define success/error constructors and representation.
  - [ ] Define `Result<void>`.
  - [ ] Add optional `?` propagation for `Result<T>` / `Option<T>` after explicit handling works.
  - [ ] Keep explicit handling valid; `?` is convenience syntax only.
- [ ] Add built-in `Option<T>`.
  - [ ] Define `Some(T)` / `None` or equivalent constructors.
  - [ ] Use for absent/non-ready states instead of sentinel values.
- [ ] Add planned `Promise<T>`.
  - [ ] `Promise<T>.await() -> Result<T>`.
  - [ ] `Promise<T>.poll() -> Option<Result<T>>`.
  - [ ] `Promise<T>.cancel() -> Result<void>`.
  - [ ] `Promise<T>.isDone() -> bool`.
- [ ] Add native aliases over `System.Handle<T>`.
  - [ ] `System.FS.FileHandle = System.Handle<System.FS.File>`.
  - [ ] `System.Net.SocketHandle = System.Handle<System.Net.Socket>`.
  - [ ] `System.HTTP.ServerHandle = System.Handle<System.HTTP.Server>`.
  - [ ] Equivalent aliases for all native resource families.

### IR / SIR Prerequisites

Native-blocking SIR syntax/lowering status:

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
- [x] Result/option/variant operations.
  - [x] `variant.tag`.
  - [x] `variant.payload.<T> <case>`.
  - [x] `variant.make.<T> <case>`.
  - [x] `result.ok.<T>`.
  - [x] `result.err.<T>`.
  - [x] `result.is.ok`.
  - [x] `result.is.err`.
  - [x] `result.unwrap.<T>`.
  - [x] `result.propagate.err`.
  - [x] option metadata is represented; distinct option syntax remains tracked under type-system work.
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
- [ ] Preserve generic built-in type arguments in SIR:
  - [ ] `System.Handle<T>`.
  - [ ] `Result<T>`.
  - [ ] `Option<T>`.
  - [ ] `Promise<T>`.
- [ ] Keep artifact-as-struct layout explicit in SIR type metadata.
- [ ] Keep artifact methods separate from artifact layout metadata.

### Bytecode / SBC Prerequisites

Native-blocking SBC metadata/opcodes status:

- [x] Type/metadata rows.
  - [x] `Result` type row.
  - [x] `Option` type row.
  - [ ] handle/resource metadata for `System.Handle<T>`.
  - [x] future metadata for future/task handles.
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
- [x] Result/option/variant opcodes.
  - [x] `VariantTag`, `VariantPayload<T>`, `VariantMake<T>`.
  - [x] `ResultOk<T>`, `ResultErr<T>`.
  - [x] `ResultIsOk`, `ResultIsErr`.
  - [x] `ResultUnwrap<T>`.
  - [x] `ResultPropagateErr`.
  - [x] option represented through metadata/variant surface.
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
  - [ ] wrong resource kind for handle use.
  - [x] native call result type matches metadata where metadata is available.
  - [x] explicit `Result<T>` / `Option<T>` extraction stack correctness.
  - [x] propagation opcode stack correctness.

### Documentation Prerequisites

- [x] Update `Docs/IR.md` with implemented native-facing IR syntax.
- [x] Update `Docs/Byte.md` with implemented SBC rows for results/options/futures and native-facing opcodes.
- [ ] Update `Docs/Language.md` with `System.Handle<T>`, `Result<T>`, `Option<T>`, `Promise<T>`, and optional `?`.
- [ ] Update generated native/standard docs to show canonical `Standard.*` names and aliases.

---

## Phase 1: Native ABI Contract

Artifacts are structs with methods for ABI purposes. Fields define layout; methods are language-level functions and do not affect ABI size/alignment/field order.

- [ ] Define exact ABI mapping table for every native-callable type.
- [ ] Define scalar ABI representations:
  - [ ] integer widths/sign.
  - [ ] float widths.
  - [ ] `bool` representation.
  - [ ] `char` representation.
  - [ ] enum representation.
- [ ] Define artifact-as-struct ABI.
  - [ ] field declaration order.
  - [ ] field alignment.
  - [ ] padding.
  - [ ] total size.
  - [ ] nested artifacts.
  - [ ] by-value vs by-pointer thresholds.
  - [ ] return-by-value rules.
  - [ ] tests proving methods do not affect layout.
- [ ] Define `string` and `Bytes` ABI.
  - [ ] borrowed pointer + length vs VM handle vs copied buffer.
  - [ ] lifetime rules for arguments.
  - [ ] lifetime rules for results.
- [ ] Define `System.Handle<T>` ABI.
  - [ ] lowers to opaque `NativeHandleId`.
  - [ ] native callees declare expected resource kind.
  - [ ] kind/generation/closed-state validated before use.
- [ ] Define `Result<T>` / `Option<T>` ABI.
  - [ ] VM representation.
  - [ ] native handler builders.
  - [ ] external FFI restrictions.
- [ ] Define `Promise<T>` ABI/runtime representation.
  - [ ] promise/job id payload.
  - [ ] completion state.
  - [ ] cancellation state.
  - [ ] result payload roots and GC behavior.
- [ ] Keep interpreter and future AOT on the same ABI contract.

---

## Phase 2: VM Native Core

### Resource Registry

- [ ] Add `VM/include/native/resource_registry.h`.
- [ ] Add `VM/src/native/resource_registry.cpp`.
- [ ] Define `NativeHandleId` as opaque VM value payload.
- [ ] Map each public `System.Handle<T>` to `NativeHandleId` plus resource kind/generation.
- [ ] Define `NativeResourceKind`:
  - [ ] file, directory, socket, listener, process, thread, job, channel.
  - [ ] FFI library/symbol.
  - [ ] ASM unit/object/symbol.
  - [ ] buffer, timer, watcher, terminal.
- [ ] Define `NativeResourceRecord`:
  - [ ] kind.
  - [ ] generation/version.
  - [ ] ownership flags.
  - [ ] closed flag.
  - [ ] debug label.
  - [ ] platform handle payload.
  - [ ] close/finalize callbacks.
- [ ] Validate handle kind before every use.
- [ ] Detect stale handle generation after close/reuse.
- [ ] Define behavior for:
  - [ ] double close.
  - [ ] use after close.
  - [ ] wrong-kind handle use.
  - [ ] VM shutdown with live handles.
  - [ ] native callback failure during cleanup.
- [ ] Add VM shutdown cleanup sweep.
- [ ] Tests:
  - [ ] leak cleanup on VM exit.
  - [ ] double close.
  - [ ] use-after-close.
  - [ ] wrong-kind handle.
  - [ ] many handles without stale reuse.

### Native Metadata

Extend `NativeFunctionSpec` with:

- [ ] layer: `core`, `system`, `standard`, `domain`.
- [ ] module and function name.
- [ ] parameter/return types.
- [ ] resource inputs/outputs.
- [ ] ownership transfer rules.
- [ ] cleanup behavior.
- [ ] blocking behavior.
- [ ] capability tags.
- [ ] platform availability.
- [ ] stability status.
- [ ] doc summary/examples.

### Capability Policy

- [ ] Add capability tags:
  - [ ] filesystem read/write.
  - [ ] process spawn.
  - [ ] environment read/write.
  - [ ] network client/server.
  - [ ] FFI/dynamic loading.
  - [ ] native assembly/code generation.
  - [ ] threading.
  - [ ] clock/time.
  - [ ] randomness.
  - [ ] terminal control.
- [ ] Add default CLI capability policy.
- [ ] Add stricter sandbox policy later.
- [ ] Document stable vs unsafe/system APIs.

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
- [ ] `Bytes` heap-owned byte sequence, UTF-8 conversion, hex/base64 later.

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
- [ ] Generate capability table.
- [ ] Generate resource lifecycle table.
- [ ] Generate platform availability table.
- [ ] Test every public native function has metadata.
- [ ] Test every resource-producing function declares resource kind and cleanup behavior.
- [ ] Unit tests for resource registry.
- [ ] VM tests for handle lifecycle edge cases.
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

- [ ] AOT and interpreter share the same native ABI/resource contract.
- [ ] JIT/AOT native calls use the same metadata as interpreter native calls.
- [ ] Add stack maps/root facts for native calls that can allocate or block.
- [ ] Add capability checks consistently across interpreter, JIT, and future AOT.
