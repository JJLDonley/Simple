# Simple::VM (Current Contract)

`Simple::VM` executes verified SBC modules and hosts runtime services.

Primary implementation files:

- Runtime/interpreter/FFI/JIT scaffolding: `VM/src/vm.cpp`
- Heap: `VM/src/heap.cpp`
- Public API: `VM/include/vm.h`
- Heap API: `VM/include/heap.h`

## Implemented

### Public Execution API

Implemented entry points:

```cpp
ExecResult ExecuteModule(const SbcModule& module);
ExecResult ExecuteModule(const SbcModule& module, bool verify);
ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit);
ExecResult ExecuteModule(const SbcModule& module,
                         bool verify,
                         bool enable_jit,
                         const ExecOptions& options);
```

`ExecResult` reports:

- execution status
- runtime error text
- process-style exit code
- JIT tier/counter/profiling fields used by tests and diagnostics

`ExecOptions` supports:

- `argv` for OS argument runtime behavior
- host import resolver callback
- runtime limits for max stack slots, locals, call depth, array/list size, const pool size, and code size

### Execution Flow

Implemented runtime flow:

```txt
SbcModule
  -> optional verifier gate
  -> global initialization
  -> entry frame setup
  -> interpreter loop
  -> ExecResult
```

The bytecode verifier remains the canonical safety gate. The interpreter should not be treated as accepting arbitrary unverified bytecode.

### Status Values

Implemented status values:

- `Ok`
- `Halted`
- `Trapped`
- `BadModule`

Runtime traps return `ExecStatus::Trapped` with an error message.

### Slot Model

The VM uses 64-bit slots for stack, locals, globals, and call passing:

```cpp
using Slot = uint64_t;
```

Reference null sentinel is documented and used as `0xFFFFFFFF`.

### Call Frames

Implemented call-frame behavior includes:

- direct calls
- indirect calls where bytecode/verifier signatures allow
- tail calls
- return program-counter tracking
- local range tracking
- stack-base tracking

### Opcode Execution

The interpreter implements the current SBC opcode surface, including:

- control flow: jumps, conditional jumps, jump tables, return, halt/trap behavior
- stack operations: pop/dup/swap/rot family
- constants: integer, float, bool, char, string, null
- local/global/upvalue load/store operations
- signed integer arithmetic for supported lanes
- unsigned integer arithmetic for supported lanes
- floating-point arithmetic
- comparisons
- boolean operations
- conversions
- function calls and call checks
- object/artifact allocation and field access
- closure allocation/upvalue access where emitted
- arrays
- lists
- string operations
- intrinsics
- syscalls/import dispatch
- line/profile/debug opcodes as runtime-recognized operations

### Heap/Object Model

Implemented heap object kinds:

```cpp
String
Array
List
Artifact
Closure
```

Heap objects have:

- header: kind, payload size, type id, mark/alive flags
- raw byte payload

Implemented heap operations:

- allocate
- get by handle
- mark
- reset marks
- sweep

The current collector is a simple mark/sweep collector with a free list. Object layouts are implemented by VM conventions over byte payloads.

### Strings

Implemented string behavior includes:

- string constant allocation/loading
- string length
- string concatenation
- char access
- slicing
- string conversion intrinsics used by the language `@string(...)` path
- stdout/stderr print paths through intrinsics/runtime imports

### Arrays

Implemented array behavior includes:

- allocation with element type id and length
- length operation
- typed get/set for supported scalar/reference lanes
- bounds checks with runtime trap on invalid access

### Lists

Implemented list behavior includes:

- allocation with element type/capacity
- length
- typed get/set
- typed push/pop
- typed insert/remove
- clear
- capacity growth
- bounds checks with runtime trap on invalid access

Supported typed lanes include the VM lanes represented by the opcode set: `i32`, `i64`, `f32`, `f64`, and `ref`, with additional scalar support where lowered through specialized opcodes or slot-compatible paths.

### Artifacts

Implemented artifact runtime behavior includes:

- heap allocation through object opcodes
- field load/store by field metadata offset
- artifact values used by language-generated code
- by-value ABI marshalling support for extern/DL calls

Artifact layout is determined by front-end/SIR metadata and SBC field/type tables.

### Core Runtime Imports

Implemented import/runtime module surface includes:

- `core.io`
- `core.fs`
- `core.os`
- `core.log`
- `core.dl`

The language-facing reserved modules are mapped to runtime imports by the compiler. See `Docs/StdLib.md` for the user-facing API names.

### Intrinsics and Syscalls

Implemented intrinsic/syscall dispatch is strict:

- unknown intrinsic ids are rejected by the verifier/runtime path
- unknown syscall/import ids are rejected or trapped rather than guessed
- intrinsic signatures are mirrored in verifier checks

Implemented intrinsic groups include:

- trap/breakpoint/logging
- numeric min/max/abs/sqrt
- time/random helpers
- stdout/stderr writing
- printable/string conversion helpers
- DL helper calls

### Dynamic Library / FFI Support

Implemented on supported non-Windows platforms through libffi.

DL flow:

```txt
dlopen/open library
  -> load symbol
  -> use extern/import signature metadata
  -> marshal VM slots/heap objects to native ABI values
  -> ffi call
  -> marshal return value back to VM slot/object
```

Implemented ABI shapes:

- scalar numeric values
- `bool`
- `char`
- strings for supported string ABI paths
- pointers represented as integer pointer slots
- enums as scalar values
- non-recursive artifacts by value
- nested artifact flattening at the ABI boundary

Rejected/unsupported ABI shapes:

- recursive artifact by-value structs
- signatures not represented by metadata
- unsupported parameter/return kinds
- Windows dynamic calls, per current docs/runtime contract

### Verification Integration

When `verify` is true, `ExecuteModule` runs the SBC verifier before execution. The VM is designed around verified bytecode assumptions:

- valid instruction boundaries
- valid table indices
- valid jump targets
- stack discipline
- compatible call signatures
- initialized local/global behavior as checked by verifier

### JIT Instrumentation and Scaffolding

Implemented JIT-related structures include:

- tier enum: `None`, `Tier0`, `Tier1`
- call thresholds
- opcode-count threshold
- call counters
- compile counters
- dispatch counters
- tier execution counters

The interpreter is still the canonical correctness path.

## In Progress

These parts exist but should be treated as under active hardening rather than final stable API:

- JIT tier execution and opcode coverage
- precise JIT eligibility documentation
- broader JIT parity with interpreter behavior
- complete closure/upvalue feature coverage across all language forms
- richer GC root tracing and stress coverage for all object graph shapes
- broader dynamic library parity across platforms
- richer runtime diagnostics with stable error codes
- formal memory/resource limits for stack, heap, frames, and object sizes
- complete public contract for `ExecOptions::argv` behavior

## Future

Not implemented as stable VM contract:

- full-surface optimizing JIT
- AOT native backend
- generational or incremental GC
- user-configurable GC policy/tuning
- Windows `core.dl` parity
- sandboxing/security isolation for untrusted bytecode
- stable VM embedding ABI beyond the current C++ API
- stable profiler/debugger protocol
- thread/concurrency runtime

## Runtime Invariants

- Verified modules must not execute undefined bytecode behavior.
- Signature mismatches are rejected; they are not coerced at runtime.
- Invalid table references are loader/verifier errors.
- Invalid heap handles, null dereferences, and out-of-bounds accesses trap.
- Dynamic native calls use declared metadata; runtime never guesses ABI shape.
- Interpreter correctness is canonical; JIT must fall back or preserve interpreter semantics.
