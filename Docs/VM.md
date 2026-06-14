# Simple VM

`Simple::VM` executes verified SBC modules and hosts runtime services.

## Scope

```txt
SbcModule -> optional Byte verification -> interpreter/runtime -> ExecResult
```

The interpreter is the correctness baseline. JIT behavior is documented separately in `Docs/JIT.md`.

## Owned files

- Public API: `VM/include/vm.h`, `VM/include/simple_api.h`
- Runtime entry/orchestration: `VM/src/vm.cpp`
- Interpreter: `VM/include/interpreter/`, `VM/src/interpreter/`
- Runtime helpers: `VM/include/runtime/`, `VM/src/runtime/`
- Heap: `VM/include/heap.h`, `VM/src/heap.cpp`
- Native modules: `VM/include/native/`, `VM/src/native/`
- FFI/DL runtime: `VM/include/ffi/`, `VM/src/ffi/`
- GC helpers: `VM/include/gc/`, `VM/src/gc/`

## Public API

```cpp
ExecResult ExecuteModule(const SbcModule& module);
ExecResult ExecuteModule(const SbcModule& module, bool verify);
ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit);
ExecResult ExecuteModule(const SbcModule& module,
                         bool verify,
                         bool enable_jit,
                         const ExecOptions& options);
```

`ExecResult` reports status, error text, exit code, and execution counters used by diagnostics/tests. `ExecOptions` carries argv, host import resolver, runtime limits, and `force_interpreter`.

## Runtime model

- Slots are 64-bit VM values. Packing/unpacking lives in `VM/include/runtime/values.h`.
- Frames track function id, pc, locals, stack boundaries, and return metadata.
- Interpreter dispatch owns opcode execution, stack operations, frames, locals/globals, calls/tailcalls, and traps.
- Runtime limits defend stack slots, locals, call depth, sequence size, const-pool size, and code size.
- Execution stats are attached by `VM/src/runtime/execution_stats.cpp`.

## Heap/object model

The heap stores strings, arrays, lists, artifacts, closures, and runtime object payloads. Helpers in `VM/src/heap.cpp` own payload reads/writes, list capacity, string creation, and string decoding/encoding.

## Imports and native modules

Reserved language imports map to runtime modules documented in `Docs/StdLib.md`. Native metadata dispatch is owned by `VM/src/native/dispatch.cpp` and registry metadata in `VM/src/native/registry.cpp`.

Dynamic library calls (`System.dl.call$...`) are owned by `VM/src/ffi/dl_call.cpp`. Recursive artifact ABI remains unsupported.

## Forbidden dependencies

- VM runtime must not depend on CLI or LSP.
- Interpreter must not own native stdlib internals, DL/FFI internals, JSON parsers, channel registries, or platform filesystem code.
- Native modules must register metadata rather than adding ad-hoc VM dispatch glue.

## Tests

VM coverage lives in:

- `Tests/tests/vm/test_*.cpp`
- `Tests/tests/test_core.cpp`
- `Tests/tests/test_jit.cpp`
- `.simple` runtime fixtures under `Tests/simple/`
