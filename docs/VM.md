# Simple VM

The Simple VM executes verified SBC modules and provides the runtime services used by compiled programs.

## Table of contents

- [Execution API](#execution-api)
- [Execution model](#execution-model)
- [Values and slots](#values-and-slots)
- [Runtime ABI classifier](#runtime-abi-classifier)
- [Frames and calls](#frames-and-calls)
- [Heap objects](#heap-objects)
- [Runtime limits](#runtime-limits)
- [Imports and native runtime](#imports-and-native-runtime)
- [Native resource lifecycle](#native-resource-lifecycle)
- [Jobs and promises](#jobs-and-promises)
- [Dynamic libraries / FFI](#dynamic-libraries--ffi)
- [Intrinsics and syscalls](#intrinsics-and-syscalls)
- [Errors and traps](#errors-and-traps)
- [Garbage collection and roots](#garbage-collection-and-roots)
- [JIT relationship](#jit-relationship)

## Execution API

```cpp
ExecResult ExecuteModule(const SbcModule& module);
ExecResult ExecuteModule(const SbcModule& module, bool verify);
ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit);
ExecResult ExecuteModule(const SbcModule& module,
                         bool verify,
                         bool enable_jit,
                         const ExecOptions& options);
```

`ExecResult` includes status, error text, exit code, call/opcode counters, and JIT counters. `ExecOptions` includes argv, host import resolution, runtime limits, and `force_interpreter`.

## Execution model

1. Optionally verify the SBC module.
2. Initialize globals and runtime state.
3. Resolve the entry method/function.
4. Execute bytecode through the interpreter unless the optional JIT path is enabled and eligible.
5. Return an `ExecResult` with status and counters.

The interpreter is the correctness baseline.

## Values and slots

VM stack/local/global values are 64-bit slots. Integers, floats, references, and bit patterns are packed into slots by the runtime value helpers.

Current internal null references use the VM heap zero sentinel. Runtime code distinguishes integer zero from reference-zero by opcode/type context. The `v0.6` source language exposes no null type or literal: optional `T?`, raw-pointer zero guards, and type-defined ZII states map onto internal representations without making the heap sentinel a source value.

## Runtime ABI classifier

`source/VM/include/runtime/abi.h` maps current SBC `TypeKind` values to ABI classes, sizes, and alignments. Canonical runtime type identities provide deterministic strings for primitive, enum, pointer, bytes, array/list, function/closure, reference, handle, channel, stable aggregate, experimental option, result, promise, and instantiated type arguments used by monomorphic generic specialization. Generic symbol helpers produce human/debug names, escaped hash-suffixed link names, and collision diagnostics. TAST generic metadata collection records generic functions, data/artifacts, and methods before later monomorphic specialization.

Scalar integer/bool/char types use exact-width ABI payloads (`bool` is one byte, `char` is four bytes at the ABI boundary), enums use their declared integer underlying type or default to `i32`, floats use IEEE-754 widths, refs/strings/arrays/lists/functions are opaque VM references (not raw host pointers), and small no-reference aggregates are eligible for by-value internal passing when their stable layout is at most 16 bytes. Scalar value validation rejects non-canonical bool payloads and invalid Unicode scalar char payloads. Stable aggregate layout keeps declaration order, ignores data methods, supports nested aggregate fields and fixed arrays, aligns each field with max alignment capped at 8 for stable data, records zero-initialized padding ranges, rounds total size to aggregate alignment, computes a deterministic layout hash, and classifies ref-containing aggregates as by-reference for native calls. Runtime ABI helpers can map stable SBC data type rows through recursive field layout classification. Recursive value containment is rejected by ABI containment validation; recursion through pointer/ref/handle-like indirection is allowed. ABI pass-mode helpers classify parameter and return values with the same direct/indirect rules. The current experimental `Result<T,E>`/`Option<T>` ABI helpers use a 16-byte `AbiVariantValue` with a tag word and one inline payload word; larger or ref-containing payloads are represented by VM-owned boxes/refs in that payload word. The `v0.6` contract removes `Option<T>` plus `Some`/`None`/`Ok`/`Err` identities directly, replacing them with optional type `T?`, absent/present metadata, Result value/error metadata, and no compatibility alias. General optional values retain an explicit discriminator; external-C `T*?` alone may use the address-zero niche. `Promise<T>` lowers to a packed generational `AbiPromiseId { index, generation }` payload. `PromiseRegistry` tracks pending, done, failed, and canceled records, cancellation requests, waiter ids, ref payload roots for GC, and rejects stale ids after slot reuse. Borrowed string ABI views use `SimpleStringView { data, size, encoding }` with UTF-8 encoding; borrowed byte ABI views use `SimpleBytesView { data, size }`. Borrowed views are call-duration only and must have non-null data when size is non-zero; `NativeCallContext::ArgStringView` exposes a call-duration UTF-8 string view and `NativeCallContext::ArgBytesView` exposes a borrowed view over heap-owned `Bytes`. Heap-owned immutable bytes use `ObjectKind::Bytes` with a length-prefixed payload and are distinct from mutable/list-style buffers. Byte-consuming native APIs accept `Bytes` as the canonical representation while still supporting existing array/list buffers where those APIs are explicitly mutable. Existing language-facing byte APIs continue to return their current list-compatible shape until the standard-library `Bytes` surface is switched over.

## Frames and calls

Each call frame tracks function id, program counter, locals, stack boundaries, return behavior, and closure/self context where needed.

Supported call forms include direct calls, indirect calls, tail calls, imported calls, and runtime intrinsic/syscall dispatch.

## Heap objects

The heap stores runtime objects such as:

- strings
- arrays
- lists
- artifacts
- closures
- native/runtime payload objects

Strings are stored as VM string payloads and converted at API boundaries. Lists grow dynamically for list operations. Arrays remain fixed-size.

## Runtime limits

`ExecOptions` can enforce limits for stack slots, locals, call depth, array/list sizes, constant-pool size, and code size. Violations become runtime errors instead of host crashes.

## Imports and native runtime

Language imports map to VM runtime modules. `System.*` names are canonical. The language-facing standard-library surface is documented in `docs/Language.md`.

Native modules include time, filesystem/path/env/OS helpers, logging, buffers, JSON, channels, random values, and threads where implemented by tests.

Native host resources use runtime-owned generational opaque handles. SBC type metadata can mark opaque handle layouts with the language-neutral type flag `0x4` and a stable native resource kind id in the type row reserved field; runtime ABI classification maps those rows to 8-byte handle values, and the native layer maps the same rows back to `NativeResourceKind`. `System.Handle<T>` lowers to a packed `NativeHandleId { index, generation, owner }` VM word. The owner identity prevents a handle created by one runtime registry from being accepted by another.

Native function metadata records layer, module, symbol, parameter/result types, resource uses, ownership transfer, cleanup behavior, blocking behavior, allocation behavior, GC/safepoint behavior, capability tags, platform availability, stability, and documentation summaries. `NativeCallContext` exposes typed argument accessors for scalar slots, canonical bool/char values, references, native handles, validated resource handles, borrowed string/byte views, and strings, and `NativeCallResult` exposes typed result builders for scalar slots, canonical bool/char values, references, native handles, strings, void, and errors. `ValidateNativeRegistryMetadata` checks required metadata invariants and uses the runtime ABI verifier to reject non-callable native signatures. `ValidateExternalCAbiSignature` applies the stricter external C FFI subset, permitting primitive/pointer payloads while rejecting VM refs, managed strings, closures, generic result/optional values, and implicit `string -> char*` coercion. The `v0.6` target adds verified `usize`/`isize`, `T*?` C-null niche marshaling, pointer/function-pointer provenance, mutability, owner/lifetime metadata, and exact callback calling conventions; no other general optional value crosses external C directly. `ValidateExternalCAbiTypeInfos` also permits stable no-reference aggregate layouts after layout computation and explicit wrapper types for C strings, string views, and bytes views. Dynamic library call dispatch builds this ABI metadata and validates it before preparing or invoking libffi. The interpreter and JIT use this shared metadata instead of ad hoc native signatures. Metadata dispatch validates declared input/inout resource handles against `NativeResourceRegistry` before invoking native handlers when a registry is supplied by the host runtime. String results returned by native handlers are allocated as VM-owned heap strings before being exposed to bytecode. Direct native binding is opt-in metadata and is considered safe only for non-blocking, non-allocating, no-resource functions with no GC safepoint. `ExecOptions::capability_policy` defaults to allow-all for current CLI compatibility, and metadata dispatch rejects calls whose capability tags are not allowed by a stricter host policy. Current tags cover filesystem access, path metadata filesystem queries, FFI dynamic loading, environment read/write, process argument access, threading, clock/time, and randomness. Native stability metadata distinguishes stable discovery helpers such as platform/architecture queries from unsafe system interfaces such as dynamic library loading.

## Native resource lifecycle

Each runtime registry owns its resource slots. A live record contains a stable resource kind, slot generation, runtime owner identity, ownership flag, closed state, an optional type-erased smart-pointer payload, and an optional close callback. Handles never contain raw host handles or host pointers.

Resource operations follow these rules:

1. Creation transfers an owned host resource into the registry and returns an opaque packed handle.
2. Every use validates non-null encoding, runtime owner, slot index, generation, expected kind, and open state before the native handler runs.
3. Explicit close invokes the close callback once, marks the record closed, and resets the owned payload. A second close reports `resource already closed`.
4. Reusing a closed slot increments its generation, so old handles report `stale handle`.
5. Runtime shutdown visits every remaining owned live record. Close failures are counted, but all records are marked closed and every owned payload is released, making cleanup deterministic and non-throwing.

`System.FS` integer descriptors remain surface-level indexes for the current library signature, but each entry resolves to an owned registry file handle before access. `System.FFI.open` returns an opaque registry handle rather than a platform library handle; `sym` and `close` validate that handle and shutdown closes any library the program leaves open. Symbols returned by `sym` are borrowed external-C addresses and are valid only while their owning library remains open. `System.Json.parse` values and every typed `System.Channel` family also use the same registry, so explicit `free`/`close`, stale-handle checks, runtime ownership, and shutdown cleanup follow one lifecycle instead of process-global handle ownership.

Native string, byte, and array inputs remain VM-owned for the duration described by their ABI view. `SimpleStringView` and `SimpleBytesView` are borrow-only and must not be retained after the native call. A native function that needs data beyond the call must copy it into resource-owned storage. VM references, managed strings, and mutable heap payload pointers must not cross worker/thread boundaries without an explicit rooted ownership design.

## Jobs and promises

The synchronized transitional `PromiseRegistry` keeps pending, completed, failed, and cancelled records stable until their owning `Job` resource closes. Completion, cancellation, polling, and blocking waits synchronize through a registry mutex and condition variable. Job shutdown wakes timers before joining workers, so abandoned long-delay jobs do not delay VM teardown.

The public experimental boundary carries only copied `i64` results and copied failure strings. Worker threads never access the heap, VM frames, globals, closures, or other resource handles. Native metadata marks job creation as host-allocating, `await` as blocking, and all async state boundaries as potential safepoints. See [Jobs and promises](Async.md).

## Dynamic libraries / FFI

`System.FFI` supports dynamic-library calls on supported platforms through declared extern metadata. The runtime validates library handles, argument count and types, return shape, and ABI support. Recursive artifact ABI is rejected. Closing or shutting down the owning runtime releases the platform dynamic-library handle.

The final external-C pointer contract is part of `v0.6` language completion, not later library work. Raw `T*` zero is deterministic but non-dereferenceable; nullable C pointers use `T*?` and lower absent/present through the C address-zero niche. Pointer-width integers, callbacks, pointee mutability, provenance, extent, ownership, deallocator, and borrow lifetime remain verifier metadata. VM references and raw pointers are never interchangeable. See [Extern declarations and FFI ABI](Language.md#extern-declarations-and-ffi-abi).

## Intrinsics and syscalls

The VM implements internal intrinsics for common runtime behavior such as time, random placeholders, printing, string conversion, object/list operations, and selected system/runtime hooks.

## Errors and traps

Invalid bytecode, stack underflow, type mismatch, invalid heap references, unsupported imports, and runtime-limit violations produce trap/error text in `ExecResult`. The VM should fail closed even when verification is disabled.

## Garbage collection and roots

Runtime root tracing covers stacks, locals, globals, closures, heap references, and GC stack-map helpers. Tests include heap and GC stress scenarios.

## JIT relationship

The JIT is optional. It tracks hot functions and may execute an eligible compiled subset, but it must preserve interpreter semantics and safe fallback behavior. See `docs/JIT.md`.
