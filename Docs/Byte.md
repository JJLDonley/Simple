# Simple Bytecode

`Simple::Byte` owns the SBC binary format, opcode metadata, loading, and verification.

## Scope

```txt
SIR/IR emission -> SBC bytes -> Byte loader -> Byte verifier -> VM
```

## Owned files

- SBC types: `Byte/include/sbc_types.h`
- Opcode enum and metadata: `Byte/include/opcode.h`, `Byte/src/opcode.cpp`
- Loader: `Byte/include/sbc_loader.h`, `Byte/src/sbc_loader.cpp`
- Verifier: `Byte/include/sbc_verifier.h`, `Byte/src/sbc_verifier.cpp`
- Emitter helpers: `Byte/include/sbc_emitter.h`

## Public contract

SBC modules contain:

- header with `SBC0` magic, version `0x0001`, endian marker, flags, section count, table offset, and entry method id
- section table for strings, types, signatures, methods, functions, globals, constants, imports, exports, and code
- compact metadata rows defined in `sbc_types.h`
- opcode stream defined by `OpCode` and opcode metadata

External producers must follow the compatibility constants in `Docs/Compatibility.md`.

## Loader

The loader validates binary structure, section bounds, row sizes, code ranges, string decoding, and table consistency needed to construct `SbcModule`. `ReadConstPoolString` is the canonical constant-pool string decoder.

## Verifier

The verifier enforces structural and type-stack invariants before execution:

- valid method/function/signature references
- code range and operand bounds
- local/global/constant type compatibility
- branch target validity and stack-shape compatibility
- call/import arity and return-shape compatibility
- heap/reference operation type constraints where metadata is available

Verification failures are returned as diagnostics strings and must not execute in the VM.

## Forbidden dependencies

- Byte must not depend on CLI, LSP, or VM runtime services.
- Loader/verifier must not implement language semantics; those belong in Lang.
- VM must execute verified modules but must still defend against malformed input.

## Tests

Byte coverage is exercised by:

- `Tests/tests/test_core.cpp`
- `Tests/tests/test_jit.cpp`
- VM verifier/runtime fixture tests
- SBC fixtures under `Tests/tests/fixtures/`
