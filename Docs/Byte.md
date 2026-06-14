# Simple Bytecode

SBC is the binary bytecode format executed by the Simple VM.

## Table of contents

- [Pipeline position](#pipeline-position)
- [File format overview](#file-format-overview)
- [Header](#header)
- [Sections](#sections)
- [Types and signatures](#types-and-signatures)
- [Constants and strings](#constants-and-strings)
- [Opcodes](#opcodes)
- [Loader contract](#loader-contract)
- [Verifier contract](#verifier-contract)
- [Compatibility](#compatibility)

## Pipeline position

```txt
SIR / IR module -> SBC bytes -> loader -> verifier -> VM execution
```

The bytecode layer is intentionally language-neutral. Language semantics are checked before emission; bytecode verification checks structural and stack/type safety.

## File format overview

An SBC file contains:

1. A fixed header.
2. A section table.
3. Metadata sections.
4. Code bytes.

The current magic is `SBC0`; the current binary format version is `0x0001`.

## Header

The header records:

- magic and version
- endian marker
- flags
- section count
- section table offset
- entry method id
- reserved fields for future compatibility

Consumers must reject unsupported magic/version combinations.

## Sections

SBC sections store strings, types, signatures, methods, functions, globals, constants, imports, exports, and code. Metadata rows use compact POD-style layouts defined by `sbc_types.h`.

## Types and signatures

Type rows encode primitive and reference-like VM types. Signatures describe parameter and result counts and index into type metadata.

Methods connect a symbol name/signature to a function body or import. Functions point at bytecode ranges and local metadata.

## Constants and strings

Constant-pool strings are decoded through the loader helper `ReadConstPoolString`. String data is length-delimited and must remain inside the owning section.

## Opcodes

Opcodes are defined in `Byte/include/opcode.h` with metadata in `Byte/src/opcode.cpp`. The opcode stream is a sequence of opcode bytes plus little-endian immediates.

The tables below are the full opcode plan grouped by related operation family. Implemented rows have assigned byte values from `OpCode`; incomplete rows reserve scalar operations in the public plan but still need opcode values, verifier metadata, emitter support, and VM dispatch.

`Operands` is the number of immediate bytes following the opcode byte. `Pops` and `Pushes` are the static stack-effect metadata used by the verifier; call-family opcodes carry dynamic arity in their operands/signatures, so their static stack effect is recorded as `0/0`.

### Control and frame

Program control, branches, traps, and frame enter/leave markers.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x00` | `Nop` | 0 | 0 | 0 |
| ✅ | `0x01` | `Halt` | 0 | 0 | 0 |
| ✅ | `0x02` | `Trap` | 0 | 0 | 0 |
| ✅ | `0x03` | `Breakpoint` | 0 | 0 | 0 |
| ✅ | `0x04` | `Jmp` | 4 | 0 | 0 |
| ✅ | `0x05` | `JmpTrue` | 4 | 1 | 0 |
| ✅ | `0x06` | `JmpFalse` | 4 | 1 | 0 |
| ✅ | `0x07` | `JmpTable` | 8 | 1 | 0 |
| ✅ | `0x73` | `Ret` | 0 | 0 | 0 |
| ✅ | `0x74` | `Enter` | 2 | 0 | 0 |
| ✅ | `0x75` | `Leave` | 0 | 0 | 0 |

### Stack

Operand-stack manipulation.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x10` | `Pop` | 0 | 1 | 0 |
| ✅ | `0x11` | `Dup` | 0 | 1 | 2 |
| ✅ | `0x12` | `Dup2` | 0 | 2 | 4 |
| ✅ | `0x13` | `Swap` | 0 | 2 | 2 |
| ✅ | `0x14` | `Rot` | 0 | 3 | 3 |

### Constants

Immediate constants and constant-pool references.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x18` | `ConstI8` | 1 | 0 | 1 |
| ✅ | `0x19` | `ConstI16` | 2 | 0 | 1 |
| ✅ | `0x1A` | `ConstI32` | 4 | 0 | 1 |
| ✅ | `0x1B` | `ConstI64` | 8 | 0 | 1 |
| ✅ | `0x1C` | `ConstI128` | 4 | 0 | 1 |
| ✅ | `0x1D` | `ConstU8` | 1 | 0 | 1 |
| ✅ | `0x1E` | `ConstU16` | 2 | 0 | 1 |
| ✅ | `0x1F` | `ConstU32` | 4 | 0 | 1 |
| ✅ | `0x20` | `ConstU64` | 8 | 0 | 1 |
| ✅ | `0x21` | `ConstU128` | 4 | 0 | 1 |
| ✅ | `0x22` | `ConstF32` | 4 | 0 | 1 |
| ✅ | `0x23` | `ConstF64` | 8 | 0 | 1 |
| ✅ | `0x24` | `ConstBool` | 1 | 0 | 1 |
| ✅ | `0x25` | `ConstChar` | 2 | 0 | 1 |
| ✅ | `0x26` | `ConstString` | 4 | 0 | 1 |
| ✅ | `0x27` | `ConstNull` | 0 | 0 | 1 |

### Locals, globals, and upvalues

Slot access for function locals, globals, and captured values.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x30` | `LoadLocal` | 4 | 0 | 1 |
| ✅ | `0x31` | `StoreLocal` | 4 | 1 | 0 |
| ✅ | `0x32` | `LoadGlobal` | 4 | 0 | 1 |
| ✅ | `0x33` | `StoreGlobal` | 4 | 1 | 0 |
| ✅ | `0x34` | `LoadUpvalue` | 4 | 0 | 1 |
| ✅ | `0x35` | `StoreUpvalue` | 4 | 1 | 0 |

### Arithmetic

Binary arithmetic operations. Missing scalar rows are part of the opcode plan but are not assigned opcode values yet.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x40` | `AddI32` | 0 | 2 | 1 |
| ✅ | `0x41` | `SubI32` | 0 | 2 | 1 |
| ✅ | `0x42` | `MulI32` | 0 | 2 | 1 |
| ✅ | `0x43` | `DivI32` | 0 | 2 | 1 |
| ✅ | `0x44` | `ModI32` | 0 | 2 | 1 |
| ✅ | `0x45` | `AddI64` | 0 | 2 | 1 |
| ✅ | `0x46` | `SubI64` | 0 | 2 | 1 |
| ✅ | `0x47` | `MulI64` | 0 | 2 | 1 |
| ✅ | `0x48` | `DivI64` | 0 | 2 | 1 |
| ✅ | `0x49` | `ModI64` | 0 | 2 | 1 |
| ✅ | `0x4A` | `AddF32` | 0 | 2 | 1 |
| ✅ | `0x4B` | `SubF32` | 0 | 2 | 1 |
| ✅ | `0x4C` | `MulF32` | 0 | 2 | 1 |
| ✅ | `0x4D` | `DivF32` | 0 | 2 | 1 |
| ✅ | `0x4E` | `AddF64` | 0 | 2 | 1 |
| ✅ | `0x4F` | `SubF64` | 0 | 2 | 1 |
| ✅ | `0x5C` | `MulF64` | 0 | 2 | 1 |
| ✅ | `0x5D` | `DivF64` | 0 | 2 | 1 |
| ✅ | `0xE1` | `AddU32` | 0 | 2 | 1 |
| ✅ | `0xE2` | `SubU32` | 0 | 2 | 1 |
| ✅ | `0xE3` | `MulU32` | 0 | 2 | 1 |
| ✅ | `0xE4` | `DivU32` | 0 | 2 | 1 |
| ✅ | `0xE5` | `ModU32` | 0 | 2 | 1 |
| ✅ | `0xE6` | `AddU64` | 0 | 2 | 1 |
| ✅ | `0xE7` | `SubU64` | 0 | 2 | 1 |
| ✅ | `0xE8` | `MulU64` | 0 | 2 | 1 |
| ✅ | `0xE9` | `DivU64` | 0 | 2 | 1 |
| ✅ | `0xEA` | `ModU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `AddI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `SubI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `MulI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `DivI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ModI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `AddI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `SubI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `MulI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `DivI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ModI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `AddI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `SubI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `MulI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `DivI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ModI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `AddU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `SubU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `MulU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `DivU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ModU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `AddU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `SubU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `MulU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `DivU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ModU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `AddU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `SubU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `MulU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `DivU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ModU128` | 0 | 2 | 1 |

### Increment and decrement

Unary increment/decrement operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x83` | `IncI32` | 0 | 1 | 1 |
| ✅ | `0x84` | `DecI32` | 0 | 1 | 1 |
| ✅ | `0x85` | `IncI64` | 0 | 1 | 1 |
| ✅ | `0x86` | `DecI64` | 0 | 1 | 1 |
| ✅ | `0x87` | `IncF32` | 0 | 1 | 1 |
| ✅ | `0x88` | `DecF32` | 0 | 1 | 1 |
| ✅ | `0x89` | `IncF64` | 0 | 1 | 1 |
| ✅ | `0x8A` | `DecF64` | 0 | 1 | 1 |
| ✅ | `0x8B` | `IncU32` | 0 | 1 | 1 |
| ✅ | `0x8C` | `DecU32` | 0 | 1 | 1 |
| ✅ | `0x8D` | `IncU64` | 0 | 1 | 1 |
| ✅ | `0x8E` | `DecU64` | 0 | 1 | 1 |
| ✅ | `0x92` | `IncI8` | 0 | 1 | 1 |
| ✅ | `0x93` | `DecI8` | 0 | 1 | 1 |
| ✅ | `0x94` | `IncI16` | 0 | 1 | 1 |
| ✅ | `0x95` | `DecI16` | 0 | 1 | 1 |
| ✅ | `0x96` | `IncU8` | 0 | 1 | 1 |
| ✅ | `0x97` | `DecU8` | 0 | 1 | 1 |
| ✅ | `0x98` | `IncU16` | 0 | 1 | 1 |
| ✅ | `0x99` | `DecU16` | 0 | 1 | 1 |
| ☐ | `TBD` | `IncI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `DecI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `IncU128` | 0 | 1 | 1 |
| ☐ | `TBD` | `DecU128` | 0 | 1 | 1 |

### Negation

Unary numeric negation operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x5E` | `NegI32` | 0 | 1 | 1 |
| ✅ | `0x5F` | `NegI64` | 0 | 1 | 1 |
| ✅ | `0x7E` | `NegF32` | 0 | 1 | 1 |
| ✅ | `0x7F` | `NegF64` | 0 | 1 | 1 |
| ✅ | `0x9A` | `NegI8` | 0 | 1 | 1 |
| ✅ | `0x9B` | `NegI16` | 0 | 1 | 1 |
| ✅ | `0x9C` | `NegU8` | 0 | 1 | 1 |
| ✅ | `0x9D` | `NegU16` | 0 | 1 | 1 |
| ✅ | `0x9E` | `NegU32` | 0 | 1 | 1 |
| ✅ | `0x9F` | `NegU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `NegI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `NegU128` | 0 | 1 | 1 |

### Comparisons

Equality and ordering comparisons.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x50` | `CmpEqI32` | 0 | 2 | 1 |
| ✅ | `0x51` | `CmpLtI32` | 0 | 2 | 1 |
| ✅ | `0x52` | `CmpNeI32` | 0 | 2 | 1 |
| ✅ | `0x53` | `CmpLeI32` | 0 | 2 | 1 |
| ✅ | `0x54` | `CmpGtI32` | 0 | 2 | 1 |
| ✅ | `0x55` | `CmpGeI32` | 0 | 2 | 1 |
| ✅ | `0x56` | `CmpEqI64` | 0 | 2 | 1 |
| ✅ | `0x57` | `CmpNeI64` | 0 | 2 | 1 |
| ✅ | `0x58` | `CmpLtI64` | 0 | 2 | 1 |
| ✅ | `0x59` | `CmpLeI64` | 0 | 2 | 1 |
| ✅ | `0x5A` | `CmpGtI64` | 0 | 2 | 1 |
| ✅ | `0x5B` | `CmpGeI64` | 0 | 2 | 1 |
| ✅ | `0x63` | `CmpEqF32` | 0 | 2 | 1 |
| ✅ | `0x64` | `CmpNeF32` | 0 | 2 | 1 |
| ✅ | `0x65` | `CmpLtF32` | 0 | 2 | 1 |
| ✅ | `0x66` | `CmpLeF32` | 0 | 2 | 1 |
| ✅ | `0x67` | `CmpGtF32` | 0 | 2 | 1 |
| ✅ | `0x68` | `CmpGeF32` | 0 | 2 | 1 |
| ✅ | `0x69` | `CmpEqF64` | 0 | 2 | 1 |
| ✅ | `0x6A` | `CmpNeF64` | 0 | 2 | 1 |
| ✅ | `0x6B` | `CmpLtF64` | 0 | 2 | 1 |
| ✅ | `0x6C` | `CmpLeF64` | 0 | 2 | 1 |
| ✅ | `0x6D` | `CmpGtF64` | 0 | 2 | 1 |
| ✅ | `0x6E` | `CmpGeF64` | 0 | 2 | 1 |
| ✅ | `0xEB` | `CmpEqU32` | 0 | 2 | 1 |
| ✅ | `0xEC` | `CmpNeU32` | 0 | 2 | 1 |
| ✅ | `0xED` | `CmpLtU32` | 0 | 2 | 1 |
| ✅ | `0xEE` | `CmpLeU32` | 0 | 2 | 1 |
| ✅ | `0xEF` | `CmpGtU32` | 0 | 2 | 1 |
| ✅ | `0xF0` | `CmpGeU32` | 0 | 2 | 1 |
| ✅ | `0xF1` | `CmpEqU64` | 0 | 2 | 1 |
| ✅ | `0xF2` | `CmpNeU64` | 0 | 2 | 1 |
| ✅ | `0xF3` | `CmpLtU64` | 0 | 2 | 1 |
| ✅ | `0xF4` | `CmpLeU64` | 0 | 2 | 1 |
| ✅ | `0xF5` | `CmpGtU64` | 0 | 2 | 1 |
| ✅ | `0xF6` | `CmpGeU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLtChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLeChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGtChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGeChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpEqBool` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNeBool` | 0 | 2 | 1 |

### Boolean logic

Boolean logical operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x60` | `BoolNot` | 0 | 1 | 1 |
| ✅ | `0x61` | `BoolAnd` | 0 | 2 | 1 |
| ✅ | `0x62` | `BoolOr` | 0 | 2 | 1 |

### Bitwise and shifts

Bitwise and shift operations. Unsigned/small/128-bit rows marked incomplete need dedicated opcode values and semantics.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0xD4` | `AndI64` | 0 | 2 | 1 |
| ✅ | `0xD5` | `OrI64` | 0 | 2 | 1 |
| ✅ | `0xD6` | `XorI64` | 0 | 2 | 1 |
| ✅ | `0xD7` | `ShlI64` | 0 | 2 | 1 |
| ✅ | `0xD8` | `ShrI64` | 0 | 2 | 1 |
| ✅ | `0xF7` | `AndI32` | 0 | 2 | 1 |
| ✅ | `0xF8` | `OrI32` | 0 | 2 | 1 |
| ✅ | `0xF9` | `XorI32` | 0 | 2 | 1 |
| ✅ | `0xFA` | `ShlI32` | 0 | 2 | 1 |
| ✅ | `0xFB` | `ShrI32` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `AndI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `OrI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `XorI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShlI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ShrI128` | 0 | 2 | 1 |

### Calls

Direct, indirect, tail, and runtime call-check operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x70` | `Call` | 5 | 0 | 0 |
| ✅ | `0x71` | `CallIndirect` | 5 | 0 | 0 |
| ✅ | `0x72` | `TailCall` | 5 | 0 | 0 |
| ✅ | `0xE0` | `CallCheck` | 0 | 0 | 0 |
| ☐ | `TBD` | `CallImport` | 5 | 0 | 0 |
| ☐ | `TBD` | `CallNative` | 5 | 0 | 0 |

### Conversions

Scalar conversion operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x76` | `ConvI32ToI64` | 0 | 1 | 1 |
| ✅ | `0x77` | `ConvI64ToI32` | 0 | 1 | 1 |
| ✅ | `0x78` | `ConvI32ToF32` | 0 | 1 | 1 |
| ✅ | `0x79` | `ConvI32ToF64` | 0 | 1 | 1 |
| ✅ | `0x7A` | `ConvF32ToI32` | 0 | 1 | 1 |
| ✅ | `0x7B` | `ConvF64ToI32` | 0 | 1 | 1 |
| ✅ | `0x7C` | `ConvF32ToF64` | 0 | 1 | 1 |
| ✅ | `0x7D` | `ConvF64ToF32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI8ToI16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI8ToI32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI8ToI64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI8ToI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI16ToI8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI16ToI32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI16ToI64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI16ToI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI32ToI8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI32ToI16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI32ToI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI64ToI8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI64ToI16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI64ToI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI128ToI8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI128ToI16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI128ToI32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI128ToI64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU8ToU16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU8ToU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU8ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU8ToU128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU16ToU8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU16ToU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU16ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU16ToU128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU32ToU8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU32ToU16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU32ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU32ToU128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToU8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToU16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToU128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU128ToU8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU128ToU16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU128ToU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU128ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI32ToU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU32ToI32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI64ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToI64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI64ToF32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvI64ToF64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToF32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU64ToF64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvF32ToI64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvF64ToI64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvF32ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvF64ToU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvCharToU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ConvU32ToChar` | 0 | 1 | 1 |

### Debug, profiling, native, and system runtime

Line/profile markers plus VM runtime/native escape hatches.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x80` | `Line` | 8 | 0 | 0 |
| ✅ | `0x81` | `ProfileStart` | 4 | 0 | 0 |
| ✅ | `0x82` | `ProfileEnd` | 4 | 0 | 0 |
| ✅ | `0x90` | `Intrinsic` | 4 | 0 | 0 |
| ✅ | `0x91` | `SysCall` | 4 | 0 | 0 |

### Objects, closures, refs, and fields

Heap object, closure, reference, and field operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0xA0` | `NewObject` | 4 | 0 | 1 |
| ✅ | `0xA1` | `NewClosure` | 5 | 0 | 1 |
| ✅ | `0xA2` | `LoadField` | 4 | 1 | 1 |
| ✅ | `0xA3` | `StoreField` | 4 | 2 | 0 |
| ✅ | `0xA4` | `IsNull` | 0 | 1 | 1 |
| ✅ | `0xA5` | `RefEq` | 0 | 2 | 1 |
| ✅ | `0xA6` | `RefNe` | 0 | 2 | 1 |
| ✅ | `0xA7` | `TypeOf` | 0 | 1 | 1 |
| ☐ | `TBD` | `CaptureLocal` | 4 | 0 | 1 |
| ☐ | `TBD` | `CaptureRef` | 4 | 0 | 1 |
| ☐ | `TBD` | `CloseUpvalue` | 4 | 0 | 0 |

### Arrays

Fixed-size array allocation and element access.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0xB0` | `NewArray` | 8 | 0 | 1 |
| ✅ | `0xB1` | `ArrayLen` | 0 | 1 | 1 |
| ✅ | `0xB2` | `ArrayGetI32` | 0 | 2 | 1 |
| ✅ | `0xB3` | `ArraySetI32` | 0 | 3 | 0 |
| ✅ | `0xB4` | `NewArrayI64` | 8 | 0 | 1 |
| ✅ | `0xB5` | `ArrayGetI64` | 0 | 2 | 1 |
| ✅ | `0xB6` | `ArraySetI64` | 0 | 3 | 0 |
| ✅ | `0xB7` | `NewArrayF32` | 8 | 0 | 1 |
| ✅ | `0xB8` | `ArrayGetF32` | 0 | 2 | 1 |
| ✅ | `0xB9` | `ArraySetF32` | 0 | 3 | 0 |
| ✅ | `0xBA` | `NewArrayF64` | 8 | 0 | 1 |
| ✅ | `0xBB` | `ArrayGetF64` | 0 | 2 | 1 |
| ✅ | `0xBC` | `ArraySetF64` | 0 | 3 | 0 |
| ✅ | `0xBD` | `NewArrayRef` | 8 | 0 | 1 |
| ✅ | `0xBE` | `ArrayGetRef` | 0 | 2 | 1 |
| ✅ | `0xBF` | `ArraySetRef` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayI8` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetI8` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayI16` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetI16` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayI128` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetI128` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayU8` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetU8` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayU16` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetU16` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayU32` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetU32` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayU64` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetU64` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayU128` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetU128` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayBool` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetBool` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetBool` | 0 | 3 | 0 |
| ☐ | `TBD` | `NewArrayChar` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGetChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySetChar` | 0 | 3 | 0 |
| ☐ | `TBD` | `ArrayCopy` | 0 | 5 | 0 |
| ☐ | `TBD` | `ArrayFill` | 0 | 3 | 0 |

### Lists

Growable list allocation and element operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x36` | `NewListRef` | 8 | 0 | 1 |
| ✅ | `0x37` | `ListGetRef` | 0 | 2 | 1 |
| ✅ | `0x38` | `ListSetRef` | 0 | 3 | 0 |
| ✅ | `0x39` | `ListPushRef` | 0 | 2 | 0 |
| ✅ | `0x3A` | `ListPopRef` | 0 | 1 | 1 |
| ✅ | `0x3B` | `ListInsertRef` | 0 | 3 | 0 |
| ✅ | `0x3C` | `ListRemoveRef` | 0 | 2 | 1 |
| ✅ | `0xA8` | `NewListF64` | 8 | 0 | 1 |
| ✅ | `0xA9` | `ListGetF64` | 0 | 2 | 1 |
| ✅ | `0xAA` | `ListSetF64` | 0 | 3 | 0 |
| ✅ | `0xAB` | `ListPushF64` | 0 | 2 | 0 |
| ✅ | `0xAC` | `ListPopF64` | 0 | 1 | 1 |
| ✅ | `0xAD` | `ListInsertF64` | 0 | 3 | 0 |
| ✅ | `0xAE` | `ListRemoveF64` | 0 | 2 | 1 |
| ✅ | `0xC0` | `NewList` | 8 | 0 | 1 |
| ✅ | `0xC1` | `ListLen` | 0 | 1 | 1 |
| ✅ | `0xC2` | `ListGetI32` | 0 | 2 | 1 |
| ✅ | `0xC3` | `ListSetI32` | 0 | 3 | 0 |
| ✅ | `0xC4` | `ListPushI32` | 0 | 2 | 0 |
| ✅ | `0xC5` | `ListPopI32` | 0 | 1 | 1 |
| ✅ | `0xC6` | `ListInsertI32` | 0 | 3 | 0 |
| ✅ | `0xC7` | `ListRemoveI32` | 0 | 2 | 1 |
| ✅ | `0xC8` | `ListClear` | 0 | 1 | 0 |
| ✅ | `0xC9` | `NewListF32` | 8 | 0 | 1 |
| ✅ | `0xCA` | `ListGetF32` | 0 | 2 | 1 |
| ✅ | `0xCB` | `ListSetF32` | 0 | 3 | 0 |
| ✅ | `0xCC` | `ListPushF32` | 0 | 2 | 0 |
| ✅ | `0xCD` | `ListPopF32` | 0 | 1 | 1 |
| ✅ | `0xCE` | `ListInsertF32` | 0 | 3 | 0 |
| ✅ | `0xCF` | `ListRemoveF32` | 0 | 2 | 1 |
| ✅ | `0xD9` | `NewListI64` | 8 | 0 | 1 |
| ✅ | `0xDA` | `ListGetI64` | 0 | 2 | 1 |
| ✅ | `0xDB` | `ListSetI64` | 0 | 3 | 0 |
| ✅ | `0xDC` | `ListPushI64` | 0 | 2 | 0 |
| ✅ | `0xDD` | `ListPopI64` | 0 | 1 | 1 |
| ✅ | `0xDE` | `ListInsertI64` | 0 | 3 | 0 |
| ✅ | `0xDF` | `ListRemoveI64` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListI8` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetI8` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushI8` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopI8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertI8` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListI16` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetI16` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushI16` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopI16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertI16` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListI128` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetI128` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushI128` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopI128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertI128` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListU8` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetU8` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushU8` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopU8` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertU8` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListU16` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetU16` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushU16` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopU16` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertU16` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListU32` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetU32` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushU32` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopU32` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertU32` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListU64` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetU64` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushU64` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopU64` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertU64` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListU128` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetU128` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushU128` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopU128` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertU128` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListBool` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetBool` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetBool` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushBool` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopBool` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertBool` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveBool` | 0 | 2 | 1 |
| ☐ | `TBD` | `NewListChar` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGetChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSetChar` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPushChar` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPopChar` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsertChar` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemoveChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListReserve` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListResize` | 0 | 3 | 0 |

### Strings

String-specific operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0xD0` | `StringLen` | 0 | 1 | 1 |
| ✅ | `0xD1` | `StringConcat` | 0 | 2 | 1 |
| ✅ | `0xD2` | `StringGetChar` | 0 | 2 | 1 |
| ✅ | `0xD3` | `StringSlice` | 0 | 3 | 1 |
| ☐ | `TBD` | `StringEq` | 0 | 2 | 1 |
| ☐ | `TBD` | `StringNe` | 0 | 2 | 1 |
| ☐ | `TBD` | `StringCompare` | 0 | 2 | 1 |
| ☐ | `TBD` | `StringFind` | 0 | 2 | 1 |
| ☐ | `TBD` | `StringToBytes` | 0 | 1 | 1 |
| ☐ | `TBD` | `BytesToString` | 0 | 1 | 1 |

### Pointer and memory

Pointer/address operations. These are planned for explicit pointer lowering instead of hiding pointer behavior behind integers or refs.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `AddressOfLocal` | 4 | 0 | 1 |
| ☐ | `TBD` | `AddressOfGlobal` | 4 | 0 | 1 |
| ☐ | `TBD` | `AddressOfField` | 4 | 1 | 1 |
| ☐ | `TBD` | `LoadPtr` | 0 | 1 | 1 |
| ☐ | `TBD` | `StorePtr` | 0 | 2 | 0 |
| ☐ | `TBD` | `PtrAdd` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrOffset` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrEq` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrNe` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrIsNull` | 0 | 1 | 1 |
| ☐ | `TBD` | `PtrCheckNull` | 0 | 1 | 1 |
| ☐ | `TBD` | `PtrCheckBounds` | 0 | 3 | 1 |

### Checked operations

Checked operations trap on overflow, invalid conversion, divide-by-zero, or out-of-bounds conditions instead of relying on unchecked opcode behavior.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `CheckedAddI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModI8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModI16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddI32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubI32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulI32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivI32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModI32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddI64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubI64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulI64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivI64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModI64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModI128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModU8` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModU16` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModU32` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModU64` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedAddU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSubU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMulU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDivU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedModU128` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedArrayGet` | 0 | TBD | TBD |
| ☐ | `TBD` | `CheckedArraySet` | 0 | TBD | TBD |
| ☐ | `TBD` | `CheckedListGet` | 0 | TBD | TBD |
| ☐ | `TBD` | `CheckedListSet` | 0 | TBD | TBD |
| ☐ | `TBD` | `CheckedStringGetChar` | 0 | TBD | TBD |
| ☐ | `TBD` | `CheckedStringSlice` | 0 | TBD | TBD |
| ☐ | `TBD` | `CheckedConvert` | 0 | TBD | TBD |

### Enums, variants, and errors

Enum/variant/result/error operations. Plain integer-like enums can continue to lower to scalar ops; these rows reserve richer tagged/payload behavior.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `EnumTag` | 0 | 1 | 1 |
| ☐ | `TBD` | `EnumPayload` | 4 | 1 | 1 |
| ☐ | `TBD` | `EnumMake` | 4 | 1 | 1 |
| ☐ | `TBD` | `VariantTag` | 0 | 1 | 1 |
| ☐ | `TBD` | `VariantPayload` | 4 | 1 | 1 |
| ☐ | `TBD` | `VariantMake` | 4 | 1 | 1 |
| ☐ | `TBD` | `ResultOk` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultErr` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultIsOk` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultIsErr` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultUnwrap` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultPropagateErr` | 0 | 1 | 1 |
| ☐ | `TBD` | `Throw` | 0 | 1 | 0 |
| ☐ | `TBD` | `Catch` | 4 | 0 | 0 |
| ☐ | `TBD` | `Finally` | 4 | 0 | 0 |
| ☐ | `TBD` | `Panic` | 0 | 1 | 0 |

### Concurrency and atomics

Thread/job/channel/atomic bytecodes. These are planned only if concurrency moves below native library calls into VM bytecode.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Spawn` | 4 | 0 | 1 |
| ☐ | `TBD` | `Join` | 0 | 1 | 1 |
| ☐ | `TBD` | `Detach` | 0 | 1 | 0 |
| ☐ | `TBD` | `Await` | 0 | 1 | 1 |
| ☐ | `TBD` | `ChannelSend` | 0 | 2 | 0 |
| ☐ | `TBD` | `ChannelRecv` | 0 | 1 | 1 |
| ☐ | `TBD` | `ChannelTryRecv` | 0 | 1 | 2 |
| ☐ | `TBD` | `AtomicLoad` | 0 | 1 | 1 |
| ☐ | `TBD` | `AtomicStore` | 0 | 2 | 0 |
| ☐ | `TBD` | `AtomicAdd` | 0 | 2 | 1 |
| ☐ | `TBD` | `AtomicSub` | 0 | 2 | 1 |
| ☐ | `TBD` | `AtomicCompareExchange` | 0 | 3 | 1 |
| ☐ | `TBD` | `Fence` | 0 | 0 | 0 |

### GC and runtime barriers

GC/runtime coordination opcodes. The current VM can perform safepoints and tracing without explicit opcodes, but these rows reserve bytecode-level barriers if needed.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Safepoint` | 0 | 0 | 0 |
| ☐ | `TBD` | `AllocCheckpoint` | 0 | 0 | 0 |
| ☐ | `TBD` | `WriteBarrier` | 0 | 2 | 0 |
| ☐ | `TBD` | `ReadBarrier` | 0 | 1 | 1 |
| ☐ | `TBD` | `PinRef` | 0 | 1 | 1 |
| ☐ | `TBD` | `UnpinRef` | 0 | 1 | 0 |
| ☐ | `TBD` | `KeepAlive` | 0 | 1 | 0 |

### Debug and introspection

Richer debug/introspection opcodes beyond the current `Line` marker and metadata tables.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Span` | 16 | 0 | 0 |
| ☐ | `TBD` | `TraceEnter` | 4 | 0 | 0 |
| ☐ | `TBD` | `TraceLeave` | 4 | 0 | 0 |
| ☐ | `TBD` | `StackTrace` | 0 | 0 | 1 |

### SIMD and vectors

Reserved vector/SIMD opcode space. These are not part of the current VM execution contract.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `VecAddF32x4` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecSubF32x4` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecMulF32x4` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecDivF32x4` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecLoad` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecStore` | 0 | 2 | 0 |
| ☐ | `TBD` | `VecSplat` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecExtract` | 0 | 2 | 1 |

## Loader contract

The loader validates binary shape:

- section table bounds
- row sizes and section sizes
- string and constant-pool bounds
- code range bounds
- table index sanity needed to construct `SbcModule`

A load failure returns an error string and must not produce a partially trusted module.

## Verifier contract

The verifier checks that code is structurally valid before execution:

- function/method/signature references are valid
- operands fit in code ranges
- branch targets are valid instruction boundaries
- stack height and stack value kinds are compatible across control flow
- locals/globals/constants are accessed with compatible types
- calls/imports use compatible arity and result shapes

The VM still performs defensive runtime checks, but verified modules are the intended execution input.

## Compatibility

Format compatibility is part of this bytecode contract. Major format changes require a version bump. Additive metadata should preserve old-reader failure behavior rather than being misread silently.
