# Simple::VM

## Scope

`Simple::VM` owns runtime execution, heap/GC, import dispatch, and FFI runtime behavior.

## Current State

Implemented and in regular use:

- Interpreter over verified SBC modules.
- Heap objects for strings/arrays/lists/artifacts/closures.
- Global/local/call-frame execution model.
- Core import handling (`core.os`, `core.fs`, `core.log`, `core.dl`).
- Dynamic DL dispatch via libffi using exact extern signature type IDs.
- By-value artifact struct marshalling for dynamic DL arguments and returns.

## Alpha Contract (Intended)

- Interpreter behavior is baseline-stable.
- Runtime traps include opcode/PC/function context.
- FFI behavior follows declared extern signatures; mismatches fail fast.

## Authoritative Runtime Contract

- Execution entrypoint is a verified SBC module entry method.
- Slot runtime is untagged; type safety is verifier+metadata enforced.
- Trap conditions are fail-fast and terminate execution with diagnostics.
- Heap references are explicit VM refs; null ref is sentinel `0xFFFFFFFF`.

## Authoritative FFI Contract

- `core.dl` dynamic calls are driven by exact extern signatures.
- Argument/return ABI types are built from SBC type IDs at runtime.
- Artifact struct arguments and returns are marshalled by-value.
- Recursive struct ABI layouts are rejected.
- Signature/type mismatches fail with deterministic runtime errors.

## Known Limits / Explicit Constraints

- Recursive struct ABI marshalling is rejected.
- Some intrinsic/syscall IDs remain unsupported by design and trap when used.
- JIT exists but still includes fallback/placeholder paths; interpreter is the stability reference.

## Primary Files

- `Simple/VM/src/vm.cpp`
- `Simple/VM/src/heap.cpp`
- `Simple/VM/include/vm.h`

## Verification Commands

- `./Simple/build.sh --suite core`
- `./Simple/build.sh --suite jit`
- `./Simple/build.sh --suite all`

## Legacy Migration Notes

Historical runtime references live in `Simple/Docs/legacy/` and are non-authoritative:

- `SBC_Runtime.md`
- `SBC_Debug.md`
- `SBC_ABI.md`
