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

Null references use the VM heap null sentinel. Runtime code distinguishes integer zero from null references by opcode/type context.

## Runtime ABI classifier

`VM/include/runtime/abi.h` maps current SBC `TypeKind` values to ABI classes, sizes, and alignments. Scalar integer/bool/char types use exact-width ABI payloads (`bool` is one byte, `char` is four bytes at the ABI boundary), floats use IEEE-754 widths, refs/strings/arrays/lists/functions are opaque VM references, and small no-reference aggregates are eligible for by-value internal passing when their stable layout is at most 16 bytes. Stable aggregate layout keeps declaration order, aligns each field, rounds total size to aggregate alignment, computes a deterministic layout hash, and classifies ref-containing aggregates as by-reference for native calls. Canonical `Result<T,E>` and `Option<T>` ABI values use a 16-byte `AbiVariantValue` with a tag word and one inline payload word; larger or ref-containing payloads are represented by VM-owned boxes/refs in that payload word. `Promise<T>` lowers to a packed generational `AbiPromiseId { index, generation }` payload. `PromiseRegistry` tracks pending, done, failed, and canceled records, cancellation requests, waiter ids, ref payload roots for GC, and rejects stale ids after slot reuse. Borrowed string ABI views use `SimpleStringView { data, size, encoding }` with UTF-8 encoding; borrowed byte ABI views use `SimpleBytesView { data, size }`. Borrowed views are call-duration only and must have non-null data when size is non-zero; `NativeCallContext::ArgStringView` exposes a call-duration UTF-8 string view and `NativeCallContext::ArgBytesView` exposes a borrowed view over heap-owned `Bytes`. Heap-owned immutable bytes use `ObjectKind::Bytes` with a length-prefixed payload and are distinct from mutable/list-style buffers. Byte-consuming native APIs accept `Bytes` as the canonical representation while still supporting existing array/list buffers where those APIs are explicitly mutable. Existing language-facing byte APIs continue to return their current list-compatible shape until the standard-library `Bytes` surface is switched over.

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

Language imports map to VM runtime modules. `System.*` names are canonical. The language-facing standard-library surface is documented in `Docs/Language.md`.

Native modules include time, filesystem/path/env/OS helpers, logging, buffers, JSON, channels, random values, and threads where implemented by tests.

Native host resources use generational opaque handles. `System.Handle<T>` lowers to a packed `NativeHandleId { index, generation }` VM word. `NativeResourceRegistry` validates handle index, generation, kind, and closed state before use, and sweeps owned live resources during registry shutdown. Shutdown sweep is best-effort: close callback failures are counted, records are still marked closed, and finalize callbacks still run.

Native function metadata records layer, module, symbol, parameter/result types, resource uses, ownership transfer, cleanup behavior, blocking behavior, capability tags, platform availability, stability, and documentation summaries. `NativeCallContext` exposes typed argument accessors for scalar slots, references, native handles, borrowed string/byte views, and strings, and `NativeCallResult` exposes typed result builders for scalar slots, references, native handles, strings, void, and errors. `ValidateNativeRegistryMetadata` checks required metadata invariants and uses the runtime ABI verifier to reject non-callable native signatures. `ValidateExternalCAbiSignature` applies the stricter external C FFI subset, permitting primitive/pointer payloads while rejecting VM refs, managed strings, closures, generic result/option values, and implicit `string -> char*` coercion. The interpreter and JIT use this shared metadata instead of ad hoc native signatures. Metadata dispatch validates declared input/inout resource handles against `NativeResourceRegistry` before invoking native handlers when a registry is supplied by the host runtime. String results returned by native handlers are allocated as VM-owned heap strings before being exposed to bytecode. `ExecOptions::capability_policy` defaults to allow-all for current CLI compatibility, and metadata dispatch rejects calls whose capability tags are not allowed by a stricter host policy. Current tags cover filesystem access, FFI dynamic loading, environment read/write, process argument access, threading, clock/time, and randomness. Native stability metadata distinguishes stable discovery helpers such as platform/architecture queries from unsafe system interfaces such as dynamic library loading.

## Dynamic libraries / FFI

`System.dl` supports dynamic-library calls on supported platforms through declared extern metadata. The runtime checks argument count, argument types, return shape, and ABI support. Recursive artifact ABI is rejected.

## Intrinsics and syscalls

The VM implements internal intrinsics for common runtime behavior such as time, random placeholders, printing, string conversion, object/list operations, and selected system/runtime hooks.

## Errors and traps

Invalid bytecode, stack underflow, type mismatch, invalid heap references, unsupported imports, and runtime-limit violations produce trap/error text in `ExecResult`. The VM should fail closed even when verification is disabled.

## Garbage collection and roots

Runtime root tracing covers stacks, locals, globals, closures, heap references, and GC stack-map helpers. Tests include heap and GC stress scenarios.

## JIT relationship

The JIT is optional. It tracks hot functions and may execute an eligible compiled subset, but it must preserve interpreter semantics and safe fallback behavior. See `Docs/JIT.md`.
