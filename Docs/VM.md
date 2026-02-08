# Simple::VM (Authoritative)

## Scope

`Simple::VM` owns runtime execution, heap/GC, import dispatch, and dynamic FFI execution.

This document is authoritative for VM runtime behavior and constraints.

## Runtime Architecture

### 1) Entry + Verification Gate

Execution entrypoints (`ExecuteModule`) are defined in `VM/include/vm.h` and implemented in `VM/src/vm.cpp`.

Execution flow:

1. receive materialized `SbcModule`
2. optionally verify (`Simple::Byte::VerifyModule`)
3. initialize globals, heap, and frame state
4. run interpreter loop (JIT tiers may be attempted)
5. return `ExecResult` with status, exit code, and diagnostics

### 2) Slot Model

- runtime stack/local/global slots are untagged 64-bit values
- semantic type correctness is enforced by verifier + metadata contracts
- helpers pack/unpack numeric, float-bit, and ref values

### 3) Call/Frame Model

Each frame tracks:

- function index + return PC
- stack base
- locals base/count
- closure reference (if applicable)
- debug location fields

Supports:

- direct calls
- indirect calls via closure/sig
- tailcalls

### 4) Heap/Object Model

Implemented by `VM/src/heap.cpp` and used from `vm.cpp`.

Object kinds include:

- string
- array
- list
- artifact
- closure

Object header carries:

- kind
- size
- type_id
- GC mark/alive state

Null reference sentinel is `0xFFFFFFFF`.

### 5) Import Dispatch

Core import modules supported in runtime dispatch:

- `core.os`
- `core.io`
- `core.fs`
- `core.log`
- `core.dl`

Optional custom import hook can be injected through `ExecOptions.import_resolver`.

## Dynamic FFI / `core.dl` Design

### Manifest Call Path

Language extern manifests produce `core.dl.call$*` imports.

Runtime dispatch:

1. resolve function pointer from first arg
2. derive arg/ret ABI types from SBC `type_id` metadata
3. build libffi call interface (`ffi_cif`)
4. marshal VM args -> native ABI values
5. invoke symbol
6. marshal native return -> VM slot/object

### Struct/Artifact Marshalling

- artifact values are marshalled by-value using field metadata
- nested structs are supported
- pointer/scalar fields are converted by exact field kind
- struct returns are materialized back into VM artifact objects

Explicit limitation:

- recursive struct ABI layouts are rejected

## Authoritative Runtime Contract

- module must be verifier-valid in safe mode
- trap conditions fail fast with deterministic error text
- globals/locals/stack obey verifier contracts at runtime boundaries
- reference values are explicit and null-safe via sentinel semantics

## Authoritative FFI Contract

- extern signatures drive runtime ABI shape exactly
- mismatched pointer/type/signature uses fail at runtime with explicit errors
- by-value artifact FFI is supported for dynamic calls
- unsupported ABI combinations are rejected (not coerced)

## JIT Status

- tier counters and tier state tracking exist in runtime
- interpreter remains the baseline correctness path
- JIT includes fallback/placeholder paths; alpha stability should treat interpreter as canonical

## Primary Files

- `VM/include/vm.h`
- `VM/src/vm.cpp`
- `VM/src/heap.cpp`
- `VM/include/heap.h`

## Verification Commands

- `./build.sh --suite core`
- `./build.sh --suite jit`
- `./build.sh --suite all`

## Legacy Migration Notes

Legacy runtime docs in `Docs/legacy/` are non-authoritative references.
