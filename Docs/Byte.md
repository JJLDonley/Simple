# Simple::Byte (Current Contract)

`Simple::Byte` defines the SBC binary format, loader, opcode metadata, and verifier used by the VM.

Primary implementation files:

- SBC types: `Byte/include/sbc_types.h`
- Opcode enum/metadata: `Byte/include/opcode.h`, `Byte/src/opcode.cpp`
- Loader: `Byte/src/sbc_loader.cpp`
- Verifier: `Byte/src/sbc_verifier.cpp`
- Emitter helpers: `Byte/include/sbc_emitter.h`

## Implemented

### SBC Purpose

SBC is the binary bytecode format consumed by `Simple::VM`.

Current pipeline:

```txt
SIR text -> Simple::IR lowering -> SBC bytes -> loader -> verifier -> VM
```

### Header

Implemented header fields are defined in `SbcHeader`:

- magic: `SBC0`
- version: `0x0001`
- endian marker
- flags
- section count
- section-table offset
- entry method id
- reserved fields

Current magic/version constants:

```cpp
constexpr uint32_t kSbcMagic = 0x30434253u;
constexpr uint16_t kSbcVersion = 0x0001u;
```

### Sections

Implemented section ids:

- `Types`
- `Fields`
- `Methods`
- `Sigs`
- `ConstPool`
- `Globals`
- `Functions`
- `Code`
- `Debug`
- `Imports`
- `Exports`

The loader validates section bounds, alignment, overlap, and row counts/sizes.

### Metadata Tables

Implemented rows include:

- `TypeRow`
- `FieldRow`
- `MethodRow`
- `SigRow`
- `GlobalRow`
- `FunctionRow`
- `ImportRow`
- `ExportRow`
- debug rows

`SbcModule` stores loaded rows plus code, const-pool bytes, debug bytes, function import flags, and signature parameter type ids.

### Type Kinds

Implemented `TypeKind` values include:

- `Unspecified`
- `I8`, `I16`, `I32`, `I64`
- `U8`, `U16`, `U32`, `U64`
- `F32`, `F64`
- `Bool`
- `Char`
- `String`
- `Ref`

The metadata enum still contains historical `I128/U128` constants, but they are not language/runtime implementation targets.

### Opcode Metadata

Implemented opcode families include:

- control flow
- stack manipulation
- constants
- local/global/upvalue access
- signed integer arithmetic
- unsigned integer arithmetic
- floating-point arithmetic
- unary numeric operations
- comparisons
- boolean logic
- calls and tail calls
- conversions
- object/closure/ref operations
- array operations
- list operations
- string operations
- intrinsics
- syscalls/imports
- line/profile/debug opcodes

`GetOpInfo` provides operand width and coarse stack effect metadata. Deeper type and control-flow validation is performed by the verifier.

### Loader

Implemented loader responsibilities:

- read SBC from file or bytes
- validate header magic/version
- validate section table position and count
- validate section bounds
- validate section alignment
- reject overlapping sections
- validate row sizes/counts for table sections
- load table rows into `SbcModule`
- load code/const/debug bytes
- validate const-pool references where structurally known
- validate cross-table references where structurally known
- produce deterministic error text on failure

Public loader API is exposed through `Byte/include/sbc_loader.h`.

### Verifier

Implemented verifier responsibilities:

- validate function code ranges
- validate opcode boundaries
- validate operand availability
- validate jump targets
- validate jump-table targets
- validate stack underflow/overflow discipline
- validate stack merge compatibility
- validate local load/store indices
- validate global load/store indices
- validate type-compatible local/global access
- validate direct call signatures
- validate indirect call signatures
- validate tailcall signatures
- validate intrinsic ids/signatures
- validate syscall/import ids/signatures where represented
- validate field access metadata
- validate array/list/string/object operation shape where possible

Public verifier API is exposed through `Byte/include/sbc_verifier.h`.

### Failure Model

Invalid bytecode should fail before VM execution:

```txt
bad structure -> loader error
bad semantics/type/stack/control flow -> verifier error
```

The VM assumes bytecode has passed loader/verifier checks when verification is enabled.

## In Progress

These are implemented partially or not yet frozen as compatibility guarantees:

- formal SBC compatibility/versioning policy
- archived SBC fixture compatibility tests
- complete opcode semantic table shared by verifier and VM
- stable binary debug-section contract
- stable export-section contract for external consumers
- stable metadata flag registry
- complete documentation for every opcode operand and stack effect
- richer verifier diagnostics with stable error codes

## Future

Not currently implemented as stable bytecode contract:

- backward/forward compatibility guarantees across major SBC versions
- bytecode feature negotiation
- compressed bytecode sections
- signed/verified package artifacts
- stable third-party SBC producer API
- independent bytecode optimizer
- debug/profiling format freeze

## Current Bytecode Invariants

- `SBC0` magic and supported version are required.
- Sections must be in bounds and non-overlapping.
- Table references must be valid.
- Code must decode into known opcodes with valid operands.
- Branches must target valid instruction boundaries.
- Stack behavior must be verifier-compatible at all merge points.
- Calls must match declared signatures.
- Unknown intrinsic/syscall/import forms are rejected or trapped; they are not guessed.
