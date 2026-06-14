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

The tables below are the full robust typed opcode plan grouped by operation family. Implemented rows have assigned byte values from `OpCode`; incomplete rows reserve typed operations in the public plan but still need opcode values, verifier metadata, emitter support, and VM dispatch.

`Operands` is the number of immediate bytes following the opcode byte. `Pops` and `Pushes` are the static stack-effect metadata used by the verifier; call-family opcodes carry dynamic arity in their operands/signatures, so their static stack effect is recorded as `0/0`.

`<T>` means a typed opcode family over the relevant scalar/reference payload set instead of listing every scalar spelling. For numeric scalar families, `<T>` means the valid subset of `i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 f64`; boolean, char, ref, pointer, string, enum, and vector families state their own payload rules.

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

### Constants and data

Immediate constants, constant-pool references, and planned typed data/blob constants.

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
| ☐ | `TBD` | `ConstBytes` | 4 | 0 | 1 |
| ☐ | `TBD` | `ConstData` | 4 | 0 | 1 |
| ☐ | `TBD` | `LoadDataRef` | 4 | 0 | 1 |

### Locals, globals, upvalues, and module init

Slot access plus planned typed module/global initialization operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x30` | `LoadLocal` | 4 | 0 | 1 |
| ✅ | `0x31` | `StoreLocal` | 4 | 1 | 0 |
| ✅ | `0x32` | `LoadGlobal` | 4 | 0 | 1 |
| ✅ | `0x33` | `StoreGlobal` | 4 | 1 | 0 |
| ✅ | `0x34` | `LoadUpvalue` | 4 | 0 | 1 |
| ✅ | `0x35` | `StoreUpvalue` | 4 | 1 | 0 |
| ☐ | `TBD` | `InitGlobal` | 4 | 0 | 0 |
| ☐ | `TBD` | `InitModule` | 4 | 0 | 0 |
| ☐ | `TBD` | `EnsureModuleInit` | 4 | 0 | 0 |

### Arithmetic

Binary arithmetic. `<T>` covers all numeric scalar types: `i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 f64` where the operation is valid.

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
| ☐ | `TBD` | `Add<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Sub<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Mul<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Div<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Mod<T>` | 0 | 2 | 1 |

### Increment and decrement

Unary increment/decrement. `<T>` covers numeric scalar types where inc/dec is valid.

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
| ☐ | `TBD` | `Inc<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `Dec<T>` | 0 | 1 | 1 |

### Negation

Unary numeric negation. `<T>` covers signed integer and floating scalar types; unsigned negation remains a policy decision.

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
| ☐ | `TBD` | `Neg<T>` | 0 | 1 | 1 |

### Comparisons

Equality and ordering comparisons. `<T>` covers all comparable scalar types, plus refs/strings where specified by type metadata.

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
| ☐ | `TBD` | `CmpEq<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpNe<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLt<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpLe<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGt<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CmpGe<T>` | 0 | 2 | 1 |

### Boolean logic

Boolean logical operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x60` | `BoolNot` | 0 | 1 | 1 |
| ✅ | `0x61` | `BoolAnd` | 0 | 2 | 1 |
| ✅ | `0x62` | `BoolOr` | 0 | 2 | 1 |

### Bitwise and shifts

Bitwise and shift operations. `<T>` covers integer scalar types only.

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
| ☐ | `TBD` | `And<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Or<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Xor<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Shl<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `Shr<T>` | 0 | 2 | 1 |

### Calls

Direct, indirect, tail, import/native, method, and virtual call forms. Typed calls reference signature metadata.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x70` | `Call` | 5 | 0 | 0 |
| ✅ | `0x71` | `CallIndirect` | 5 | 0 | 0 |
| ✅ | `0x72` | `TailCall` | 5 | 0 | 0 |
| ✅ | `0xE0` | `CallCheck` | 0 | 0 | 0 |
| ☐ | `TBD` | `CallImport` | 5 | 0 | 0 |
| ☐ | `TBD` | `CallNative` | 5 | 0 | 0 |
| ☐ | `TBD` | `CallMethod` | 5 | 0 | 0 |
| ☐ | `TBD` | `CallVirtual` | 5 | 0 | 0 |

### Conversions

Scalar conversions. `<From,To>` covers explicit typed conversion pairs. Checked variants trap on invalid/narrowing overflow.

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
| ☐ | `TBD` | `Conv<From,To>` | 0 | 1 | 1 |
| ☐ | `TBD` | `CheckedConv<From,To>` | 0 | 1 | 1 |

### Debug, profiling, native, and system runtime

Line/profile markers plus VM runtime/native escape hatches.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x80` | `Line` | 8 | 0 | 0 |
| ✅ | `0x81` | `ProfileStart` | 4 | 0 | 0 |
| ✅ | `0x82` | `ProfileEnd` | 4 | 0 | 0 |
| ✅ | `0x90` | `Intrinsic` | 4 | 0 | 0 |
| ✅ | `0x91` | `SysCall` | 4 | 0 | 0 |
| ☐ | `TBD` | `Span` | 16 | 0 | 0 |
| ☐ | `TBD` | `TraceEnter` | 4 | 0 | 0 |
| ☐ | `TBD` | `TraceLeave` | 4 | 0 | 0 |
| ☐ | `TBD` | `StackTrace` | 0 | 0 | 1 |

### Objects, closures, refs, and fields

Heap object, closure, reference, field, object lifecycle, and typed reference operations.

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
| ☐ | `TBD` | `InitObject` | 4 | 1 | 1 |
| ☐ | `TBD` | `DropObject` | 0 | 1 | 0 |
| ☐ | `TBD` | `CloneObject` | 0 | 1 | 1 |
| ☐ | `TBD` | `ObjectEq` | 0 | 2 | 1 |
| ☐ | `TBD` | `InstanceOf<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `CastRef<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `CheckedCastRef<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `LoadVTable` | 0 | 1 | 1 |

### Arrays

Fixed-size array allocation and element access. `<T>` covers every scalar payload plus `ref`.

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
| ☐ | `TBD` | `NewArray<T>` | 8 | 0 | 1 |
| ☐ | `TBD` | `ArrayGet<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `ArraySet<T>` | 0 | 3 | 0 |
| ☐ | `TBD` | `ArrayCopy<T>` | 0 | 5 | 0 |
| ☐ | `TBD` | `ArrayFill<T>` | 0 | 3 | 0 |

### Lists

Growable list allocation and element operations. `<T>` covers every scalar payload plus `ref`.

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
| ☐ | `TBD` | `NewList<T>` | 8 | 0 | 1 |
| ☐ | `TBD` | `ListGet<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `ListSet<T>` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListPush<T>` | 0 | 2 | 0 |
| ☐ | `TBD` | `ListPop<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `ListInsert<T>` | 0 | 3 | 0 |
| ☐ | `TBD` | `ListRemove<T>` | 0 | 2 | 1 |
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

Explicit pointer/address and raw memory operations. `<T>` load/store rows are typed by pointee/value type.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `AddressOfLocal` | 4 | 0 | 1 |
| ☐ | `TBD` | `AddressOfGlobal` | 4 | 0 | 1 |
| ☐ | `TBD` | `AddressOfField` | 4 | 1 | 1 |
| ☐ | `TBD` | `LoadPtr<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `StorePtr<T>` | 0 | 2 | 0 |
| ☐ | `TBD` | `PtrAdd` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrOffset` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrEq` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrNe` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrIsNull` | 0 | 1 | 1 |
| ☐ | `TBD` | `PtrCheckNull` | 0 | 1 | 1 |
| ☐ | `TBD` | `PtrCheckBounds` | 0 | 3 | 1 |
| ☐ | `TBD` | `MemCopy` | 0 | 3 | 0 |
| ☐ | `TBD` | `MemMove` | 0 | 3 | 0 |
| ☐ | `TBD` | `MemSet` | 0 | 3 | 0 |
| ☐ | `TBD` | `MemCompare` | 0 | 3 | 1 |

### Checked operations

Checked arithmetic, bounds, null, and conversion operations. `<T>` covers numeric scalar types valid for the operation.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `CheckedAdd<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedSub<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMul<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedDiv<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedMod<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedArrayGet<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedArraySet<T>` | 0 | 3 | 0 |
| ☐ | `TBD` | `CheckedListGet<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedListSet<T>` | 0 | 3 | 0 |
| ☐ | `TBD` | `CheckedStringGetChar` | 0 | 2 | 1 |
| ☐ | `TBD` | `CheckedStringSlice` | 0 | 3 | 1 |
| ☐ | `TBD` | `CheckedNull` | 0 | 1 | 1 |
| ☐ | `TBD` | `CheckedBounds` | 0 | 3 | 1 |

### Enums, variants, and errors

Enum/variant/result/error operations. Plain integer-like enums may still lower to scalar ops.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `EnumTag` | 0 | 1 | 1 |
| ☐ | `TBD` | `EnumPayload<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `EnumMake<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `VariantTag` | 0 | 1 | 1 |
| ☐ | `TBD` | `VariantPayload<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `VariantMake<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `ResultOk<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultErr<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultIsOk` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultIsErr` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultUnwrap<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `ResultPropagateErr` | 0 | 1 | 1 |
| ☐ | `TBD` | `Throw` | 0 | 1 | 0 |
| ☐ | `TBD` | `Catch` | 4 | 0 | 0 |
| ☐ | `TBD` | `Finally` | 4 | 0 | 0 |
| ☐ | `TBD` | `Panic` | 0 | 1 | 0 |

### Range and iterators

Typed range/iterator bytecodes for loop lowering when not expanded into scalar loop code.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `RangeNew<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `RangeNewStep<T>` | 0 | 3 | 1 |
| ☐ | `TBD` | `RangeNext<T>` | 0 | 1 | 2 |
| ☐ | `TBD` | `IteratorNext<T>` | 0 | 1 | 2 |
| ☐ | `TBD` | `IteratorHasNext` | 0 | 1 | 1 |
| ☐ | `TBD` | `IteratorValue<T>` | 0 | 1 | 1 |

### Concurrency and atomics

Thread/job/channel/atomic bytecodes. `<T>` covers channel/atomic payload types when concurrency moves below native library calls.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Spawn` | 4 | 0 | 1 |
| ☐ | `TBD` | `Join` | 0 | 1 | 1 |
| ☐ | `TBD` | `Detach` | 0 | 1 | 0 |
| ☐ | `TBD` | `Await` | 0 | 1 | 1 |
| ☐ | `TBD` | `Yield` | 0 | 0 | 0 |
| ☐ | `TBD` | `Resume` | 0 | 1 | 0 |
| ☐ | `TBD` | `Suspend` | 0 | 0 | 1 |
| ☐ | `TBD` | `MakeFuture` | 4 | 0 | 1 |
| ☐ | `TBD` | `PollFuture` | 0 | 1 | 2 |
| ☐ | `TBD` | `ChannelSend<T>` | 0 | 2 | 0 |
| ☐ | `TBD` | `ChannelRecv<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `ChannelTryRecv<T>` | 0 | 1 | 2 |
| ☐ | `TBD` | `AtomicLoad<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `AtomicStore<T>` | 0 | 2 | 0 |
| ☐ | `TBD` | `AtomicAdd<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `AtomicSub<T>` | 0 | 2 | 1 |
| ☐ | `TBD` | `AtomicCompareExchange<T>` | 0 | 3 | 1 |
| ☐ | `TBD` | `Fence` | 0 | 0 | 0 |

### Locks and monitors

Monitor/lock operations for VM-managed synchronization if needed.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Lock` | 0 | 1 | 0 |
| ☐ | `TBD` | `Unlock` | 0 | 1 | 0 |
| ☐ | `TBD` | `TryLock` | 0 | 1 | 1 |
| ☐ | `TBD` | `Wait` | 0 | 1 | 0 |
| ☐ | `TBD` | `Notify` | 0 | 1 | 0 |
| ☐ | `TBD` | `NotifyAll` | 0 | 1 | 0 |

### GC and runtime barriers

GC/runtime coordination opcodes. The current VM can perform safepoints and tracing without explicit opcodes, but these reserve bytecode-level barriers if needed.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Safepoint` | 0 | 0 | 0 |
| ☐ | `TBD` | `AllocCheckpoint` | 0 | 0 | 0 |
| ☐ | `TBD` | `WriteBarrier` | 0 | 2 | 0 |
| ☐ | `TBD` | `ReadBarrier` | 0 | 1 | 1 |
| ☐ | `TBD` | `PinRef` | 0 | 1 | 1 |
| ☐ | `TBD` | `UnpinRef` | 0 | 1 | 0 |
| ☐ | `TBD` | `KeepAlive` | 0 | 1 | 0 |

### JIT and deoptimization

JIT patching, guards, and deoptimization hooks for a typed optimizing backend.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `Deopt` | 4 | 0 | 0 |
| ☐ | `TBD` | `Patchpoint` | 4 | 0 | 0 |
| ☐ | `TBD` | `InlineCache` | 4 | 0 | 0 |
| ☐ | `TBD` | `GuardType<T>` | 4 | 1 | 1 |
| ☐ | `TBD` | `GuardBounds` | 0 | 3 | 1 |
| ☐ | `TBD` | `GuardNotNull` | 0 | 1 | 1 |

### Capabilities and sandboxing

Optional capability/security checks for restricted runtimes.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `CheckCapability` | 4 | 0 | 0 |
| ☐ | `TBD` | `EnterSandbox` | 4 | 0 | 0 |
| ☐ | `TBD` | `ExitSandbox` | 0 | 0 | 0 |

### SIMD and vectors

Reserved vector/SIMD opcode space. `<T,N>` is element type plus lane count.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ☐ | `TBD` | `VecLoad<T,N>` | 0 | 1 | 1 |
| ☐ | `TBD` | `VecStore<T,N>` | 0 | 2 | 0 |
| ☐ | `TBD` | `VecSplat<T,N>` | 0 | 1 | 1 |
| ☐ | `TBD` | `VecExtract<T,N>` | 4 | 1 | 1 |
| ☐ | `TBD` | `VecAdd<T,N>` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecSub<T,N>` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecMul<T,N>` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecDiv<T,N>` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecAnd<T,N>` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecOr<T,N>` | 0 | 2 | 1 |
| ☐ | `TBD` | `VecXor<T,N>` | 0 | 2 | 1 |

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
