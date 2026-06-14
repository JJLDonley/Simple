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

The table below is the full opcode set currently defined by `OpCode`. `Operands` is the number of immediate bytes following the opcode byte. `Pops` and `Pushes` are the static stack-effect metadata used by the verifier; call-family opcodes carry dynamic arity in their operands/signatures, so their static stack effect is recorded as `0/0`.

| Value | Name | Operands | Pops | Pushes | Status |
|---:|---|---:|---:|---:|:---:|
| `0x00` | `Nop` | 0 | 0 | 0 | ✅ |
| `0x01` | `Halt` | 0 | 0 | 0 | ✅ |
| `0x02` | `Trap` | 0 | 0 | 0 | ✅ |
| `0x03` | `Breakpoint` | 0 | 0 | 0 | ✅ |
| `0x04` | `Jmp` | 4 | 0 | 0 | ✅ |
| `0x05` | `JmpTrue` | 4 | 1 | 0 | ✅ |
| `0x06` | `JmpFalse` | 4 | 1 | 0 | ✅ |
| `0x07` | `JmpTable` | 8 | 1 | 0 | ✅ |
| `0x10` | `Pop` | 0 | 1 | 0 | ✅ |
| `0x11` | `Dup` | 0 | 1 | 2 | ✅ |
| `0x12` | `Dup2` | 0 | 2 | 4 | ✅ |
| `0x13` | `Swap` | 0 | 2 | 2 | ✅ |
| `0x14` | `Rot` | 0 | 3 | 3 | ✅ |
| `0x18` | `ConstI8` | 1 | 0 | 1 | ✅ |
| `0x19` | `ConstI16` | 2 | 0 | 1 | ✅ |
| `0x1A` | `ConstI32` | 4 | 0 | 1 | ✅ |
| `0x1B` | `ConstI64` | 8 | 0 | 1 | ✅ |
| `0x1C` | `ConstI128` | 4 | 0 | 1 | ✅ |
| `0x1D` | `ConstU8` | 1 | 0 | 1 | ✅ |
| `0x1E` | `ConstU16` | 2 | 0 | 1 | ✅ |
| `0x1F` | `ConstU32` | 4 | 0 | 1 | ✅ |
| `0x20` | `ConstU64` | 8 | 0 | 1 | ✅ |
| `0x21` | `ConstU128` | 4 | 0 | 1 | ✅ |
| `0x22` | `ConstF32` | 4 | 0 | 1 | ✅ |
| `0x23` | `ConstF64` | 8 | 0 | 1 | ✅ |
| `0x24` | `ConstBool` | 1 | 0 | 1 | ✅ |
| `0x25` | `ConstChar` | 2 | 0 | 1 | ✅ |
| `0x26` | `ConstString` | 4 | 0 | 1 | ✅ |
| `0x27` | `ConstNull` | 0 | 0 | 1 | ✅ |
| `0x30` | `LoadLocal` | 4 | 0 | 1 | ✅ |
| `0x31` | `StoreLocal` | 4 | 1 | 0 | ✅ |
| `0x32` | `LoadGlobal` | 4 | 0 | 1 | ✅ |
| `0x33` | `StoreGlobal` | 4 | 1 | 0 | ✅ |
| `0x34` | `LoadUpvalue` | 4 | 0 | 1 | ✅ |
| `0x35` | `StoreUpvalue` | 4 | 1 | 0 | ✅ |
| `0x36` | `NewListRef` | 8 | 0 | 1 | ✅ |
| `0x37` | `ListGetRef` | 0 | 2 | 1 | ✅ |
| `0x38` | `ListSetRef` | 0 | 3 | 0 | ✅ |
| `0x39` | `ListPushRef` | 0 | 2 | 0 | ✅ |
| `0x3A` | `ListPopRef` | 0 | 1 | 1 | ✅ |
| `0x3B` | `ListInsertRef` | 0 | 3 | 0 | ✅ |
| `0x3C` | `ListRemoveRef` | 0 | 2 | 1 | ✅ |
| `0x40` | `AddI32` | 0 | 2 | 1 | ✅ |
| `0x41` | `SubI32` | 0 | 2 | 1 | ✅ |
| `0x42` | `MulI32` | 0 | 2 | 1 | ✅ |
| `0x43` | `DivI32` | 0 | 2 | 1 | ✅ |
| `0x44` | `ModI32` | 0 | 2 | 1 | ✅ |
| `0x45` | `AddI64` | 0 | 2 | 1 | ✅ |
| `0x46` | `SubI64` | 0 | 2 | 1 | ✅ |
| `0x47` | `MulI64` | 0 | 2 | 1 | ✅ |
| `0x48` | `DivI64` | 0 | 2 | 1 | ✅ |
| `0x49` | `ModI64` | 0 | 2 | 1 | ✅ |
| `0x4A` | `AddF32` | 0 | 2 | 1 | ✅ |
| `0x4B` | `SubF32` | 0 | 2 | 1 | ✅ |
| `0x4C` | `MulF32` | 0 | 2 | 1 | ✅ |
| `0x4D` | `DivF32` | 0 | 2 | 1 | ✅ |
| `0x4E` | `AddF64` | 0 | 2 | 1 | ✅ |
| `0x4F` | `SubF64` | 0 | 2 | 1 | ✅ |
| `0x50` | `CmpEqI32` | 0 | 2 | 1 | ✅ |
| `0x51` | `CmpLtI32` | 0 | 2 | 1 | ✅ |
| `0x52` | `CmpNeI32` | 0 | 2 | 1 | ✅ |
| `0x53` | `CmpLeI32` | 0 | 2 | 1 | ✅ |
| `0x54` | `CmpGtI32` | 0 | 2 | 1 | ✅ |
| `0x55` | `CmpGeI32` | 0 | 2 | 1 | ✅ |
| `0x56` | `CmpEqI64` | 0 | 2 | 1 | ✅ |
| `0x57` | `CmpNeI64` | 0 | 2 | 1 | ✅ |
| `0x58` | `CmpLtI64` | 0 | 2 | 1 | ✅ |
| `0x59` | `CmpLeI64` | 0 | 2 | 1 | ✅ |
| `0x5A` | `CmpGtI64` | 0 | 2 | 1 | ✅ |
| `0x5B` | `CmpGeI64` | 0 | 2 | 1 | ✅ |
| `0x5C` | `MulF64` | 0 | 2 | 1 | ✅ |
| `0x5D` | `DivF64` | 0 | 2 | 1 | ✅ |
| `0x5E` | `NegI32` | 0 | 1 | 1 | ✅ |
| `0x5F` | `NegI64` | 0 | 1 | 1 | ✅ |
| `0x60` | `BoolNot` | 0 | 1 | 1 | ✅ |
| `0x61` | `BoolAnd` | 0 | 2 | 1 | ✅ |
| `0x62` | `BoolOr` | 0 | 2 | 1 | ✅ |
| `0x63` | `CmpEqF32` | 0 | 2 | 1 | ✅ |
| `0x64` | `CmpNeF32` | 0 | 2 | 1 | ✅ |
| `0x65` | `CmpLtF32` | 0 | 2 | 1 | ✅ |
| `0x66` | `CmpLeF32` | 0 | 2 | 1 | ✅ |
| `0x67` | `CmpGtF32` | 0 | 2 | 1 | ✅ |
| `0x68` | `CmpGeF32` | 0 | 2 | 1 | ✅ |
| `0x69` | `CmpEqF64` | 0 | 2 | 1 | ✅ |
| `0x6A` | `CmpNeF64` | 0 | 2 | 1 | ✅ |
| `0x6B` | `CmpLtF64` | 0 | 2 | 1 | ✅ |
| `0x6C` | `CmpLeF64` | 0 | 2 | 1 | ✅ |
| `0x6D` | `CmpGtF64` | 0 | 2 | 1 | ✅ |
| `0x6E` | `CmpGeF64` | 0 | 2 | 1 | ✅ |
| `0x70` | `Call` | 5 | 0 | 0 | ✅ |
| `0x71` | `CallIndirect` | 5 | 0 | 0 | ✅ |
| `0x72` | `TailCall` | 5 | 0 | 0 | ✅ |
| `0x73` | `Ret` | 0 | 0 | 0 | ✅ |
| `0x74` | `Enter` | 2 | 0 | 0 | ✅ |
| `0x75` | `Leave` | 0 | 0 | 0 | ✅ |
| `0x76` | `ConvI32ToI64` | 0 | 1 | 1 | ✅ |
| `0x77` | `ConvI64ToI32` | 0 | 1 | 1 | ✅ |
| `0x78` | `ConvI32ToF32` | 0 | 1 | 1 | ✅ |
| `0x79` | `ConvI32ToF64` | 0 | 1 | 1 | ✅ |
| `0x7A` | `ConvF32ToI32` | 0 | 1 | 1 | ✅ |
| `0x7B` | `ConvF64ToI32` | 0 | 1 | 1 | ✅ |
| `0x7C` | `ConvF32ToF64` | 0 | 1 | 1 | ✅ |
| `0x7D` | `ConvF64ToF32` | 0 | 1 | 1 | ✅ |
| `0x7E` | `NegF32` | 0 | 1 | 1 | ✅ |
| `0x7F` | `NegF64` | 0 | 1 | 1 | ✅ |
| `0x80` | `Line` | 8 | 0 | 0 | ✅ |
| `0x81` | `ProfileStart` | 4 | 0 | 0 | ✅ |
| `0x82` | `ProfileEnd` | 4 | 0 | 0 | ✅ |
| `0x83` | `IncI32` | 0 | 1 | 1 | ✅ |
| `0x84` | `DecI32` | 0 | 1 | 1 | ✅ |
| `0x85` | `IncI64` | 0 | 1 | 1 | ✅ |
| `0x86` | `DecI64` | 0 | 1 | 1 | ✅ |
| `0x87` | `IncF32` | 0 | 1 | 1 | ✅ |
| `0x88` | `DecF32` | 0 | 1 | 1 | ✅ |
| `0x89` | `IncF64` | 0 | 1 | 1 | ✅ |
| `0x8A` | `DecF64` | 0 | 1 | 1 | ✅ |
| `0x8B` | `IncU32` | 0 | 1 | 1 | ✅ |
| `0x8C` | `DecU32` | 0 | 1 | 1 | ✅ |
| `0x8D` | `IncU64` | 0 | 1 | 1 | ✅ |
| `0x8E` | `DecU64` | 0 | 1 | 1 | ✅ |
| `0x90` | `Intrinsic` | 4 | 0 | 0 | ✅ |
| `0x91` | `SysCall` | 4 | 0 | 0 | ✅ |
| `0x92` | `IncI8` | 0 | 1 | 1 | ✅ |
| `0x93` | `DecI8` | 0 | 1 | 1 | ✅ |
| `0x94` | `IncI16` | 0 | 1 | 1 | ✅ |
| `0x95` | `DecI16` | 0 | 1 | 1 | ✅ |
| `0x96` | `IncU8` | 0 | 1 | 1 | ✅ |
| `0x97` | `DecU8` | 0 | 1 | 1 | ✅ |
| `0x98` | `IncU16` | 0 | 1 | 1 | ✅ |
| `0x99` | `DecU16` | 0 | 1 | 1 | ✅ |
| `0x9A` | `NegI8` | 0 | 1 | 1 | ✅ |
| `0x9B` | `NegI16` | 0 | 1 | 1 | ✅ |
| `0x9C` | `NegU8` | 0 | 1 | 1 | ✅ |
| `0x9D` | `NegU16` | 0 | 1 | 1 | ✅ |
| `0x9E` | `NegU32` | 0 | 1 | 1 | ✅ |
| `0x9F` | `NegU64` | 0 | 1 | 1 | ✅ |
| `0xA0` | `NewObject` | 4 | 0 | 1 | ✅ |
| `0xA1` | `NewClosure` | 5 | 0 | 1 | ✅ |
| `0xA2` | `LoadField` | 4 | 1 | 1 | ✅ |
| `0xA3` | `StoreField` | 4 | 2 | 0 | ✅ |
| `0xA4` | `IsNull` | 0 | 1 | 1 | ✅ |
| `0xA5` | `RefEq` | 0 | 2 | 1 | ✅ |
| `0xA6` | `RefNe` | 0 | 2 | 1 | ✅ |
| `0xA7` | `TypeOf` | 0 | 1 | 1 | ✅ |
| `0xA8` | `NewListF64` | 8 | 0 | 1 | ✅ |
| `0xA9` | `ListGetF64` | 0 | 2 | 1 | ✅ |
| `0xAA` | `ListSetF64` | 0 | 3 | 0 | ✅ |
| `0xAB` | `ListPushF64` | 0 | 2 | 0 | ✅ |
| `0xAC` | `ListPopF64` | 0 | 1 | 1 | ✅ |
| `0xAD` | `ListInsertF64` | 0 | 3 | 0 | ✅ |
| `0xAE` | `ListRemoveF64` | 0 | 2 | 1 | ✅ |
| `0xB0` | `NewArray` | 8 | 0 | 1 | ✅ |
| `0xB1` | `ArrayLen` | 0 | 1 | 1 | ✅ |
| `0xB2` | `ArrayGetI32` | 0 | 2 | 1 | ✅ |
| `0xB3` | `ArraySetI32` | 0 | 3 | 0 | ✅ |
| `0xB4` | `NewArrayI64` | 8 | 0 | 1 | ✅ |
| `0xB5` | `ArrayGetI64` | 0 | 2 | 1 | ✅ |
| `0xB6` | `ArraySetI64` | 0 | 3 | 0 | ✅ |
| `0xB7` | `NewArrayF32` | 8 | 0 | 1 | ✅ |
| `0xB8` | `ArrayGetF32` | 0 | 2 | 1 | ✅ |
| `0xB9` | `ArraySetF32` | 0 | 3 | 0 | ✅ |
| `0xBA` | `NewArrayF64` | 8 | 0 | 1 | ✅ |
| `0xBB` | `ArrayGetF64` | 0 | 2 | 1 | ✅ |
| `0xBC` | `ArraySetF64` | 0 | 3 | 0 | ✅ |
| `0xBD` | `NewArrayRef` | 8 | 0 | 1 | ✅ |
| `0xBE` | `ArrayGetRef` | 0 | 2 | 1 | ✅ |
| `0xBF` | `ArraySetRef` | 0 | 3 | 0 | ✅ |
| `0xC0` | `NewList` | 8 | 0 | 1 | ✅ |
| `0xC1` | `ListLen` | 0 | 1 | 1 | ✅ |
| `0xC2` | `ListGetI32` | 0 | 2 | 1 | ✅ |
| `0xC3` | `ListSetI32` | 0 | 3 | 0 | ✅ |
| `0xC4` | `ListPushI32` | 0 | 2 | 0 | ✅ |
| `0xC5` | `ListPopI32` | 0 | 1 | 1 | ✅ |
| `0xC6` | `ListInsertI32` | 0 | 3 | 0 | ✅ |
| `0xC7` | `ListRemoveI32` | 0 | 2 | 1 | ✅ |
| `0xC8` | `ListClear` | 0 | 1 | 0 | ✅ |
| `0xC9` | `NewListF32` | 8 | 0 | 1 | ✅ |
| `0xCA` | `ListGetF32` | 0 | 2 | 1 | ✅ |
| `0xCB` | `ListSetF32` | 0 | 3 | 0 | ✅ |
| `0xCC` | `ListPushF32` | 0 | 2 | 0 | ✅ |
| `0xCD` | `ListPopF32` | 0 | 1 | 1 | ✅ |
| `0xCE` | `ListInsertF32` | 0 | 3 | 0 | ✅ |
| `0xCF` | `ListRemoveF32` | 0 | 2 | 1 | ✅ |
| `0xD0` | `StringLen` | 0 | 1 | 1 | ✅ |
| `0xD1` | `StringConcat` | 0 | 2 | 1 | ✅ |
| `0xD2` | `StringGetChar` | 0 | 2 | 1 | ✅ |
| `0xD3` | `StringSlice` | 0 | 3 | 1 | ✅ |
| `0xD4` | `AndI64` | 0 | 2 | 1 | ✅ |
| `0xD5` | `OrI64` | 0 | 2 | 1 | ✅ |
| `0xD6` | `XorI64` | 0 | 2 | 1 | ✅ |
| `0xD7` | `ShlI64` | 0 | 2 | 1 | ✅ |
| `0xD8` | `ShrI64` | 0 | 2 | 1 | ✅ |
| `0xD9` | `NewListI64` | 8 | 0 | 1 | ✅ |
| `0xDA` | `ListGetI64` | 0 | 2 | 1 | ✅ |
| `0xDB` | `ListSetI64` | 0 | 3 | 0 | ✅ |
| `0xDC` | `ListPushI64` | 0 | 2 | 0 | ✅ |
| `0xDD` | `ListPopI64` | 0 | 1 | 1 | ✅ |
| `0xDE` | `ListInsertI64` | 0 | 3 | 0 | ✅ |
| `0xDF` | `ListRemoveI64` | 0 | 2 | 1 | ✅ |
| `0xE0` | `CallCheck` | 0 | 0 | 0 | ✅ |
| `0xE1` | `AddU32` | 0 | 2 | 1 | ✅ |
| `0xE2` | `SubU32` | 0 | 2 | 1 | ✅ |
| `0xE3` | `MulU32` | 0 | 2 | 1 | ✅ |
| `0xE4` | `DivU32` | 0 | 2 | 1 | ✅ |
| `0xE5` | `ModU32` | 0 | 2 | 1 | ✅ |
| `0xE6` | `AddU64` | 0 | 2 | 1 | ✅ |
| `0xE7` | `SubU64` | 0 | 2 | 1 | ✅ |
| `0xE8` | `MulU64` | 0 | 2 | 1 | ✅ |
| `0xE9` | `DivU64` | 0 | 2 | 1 | ✅ |
| `0xEA` | `ModU64` | 0 | 2 | 1 | ✅ |
| `0xEB` | `CmpEqU32` | 0 | 2 | 1 | ✅ |
| `0xEC` | `CmpNeU32` | 0 | 2 | 1 | ✅ |
| `0xED` | `CmpLtU32` | 0 | 2 | 1 | ✅ |
| `0xEE` | `CmpLeU32` | 0 | 2 | 1 | ✅ |
| `0xEF` | `CmpGtU32` | 0 | 2 | 1 | ✅ |
| `0xF0` | `CmpGeU32` | 0 | 2 | 1 | ✅ |
| `0xF1` | `CmpEqU64` | 0 | 2 | 1 | ✅ |
| `0xF2` | `CmpNeU64` | 0 | 2 | 1 | ✅ |
| `0xF3` | `CmpLtU64` | 0 | 2 | 1 | ✅ |
| `0xF4` | `CmpLeU64` | 0 | 2 | 1 | ✅ |
| `0xF5` | `CmpGtU64` | 0 | 2 | 1 | ✅ |
| `0xF6` | `CmpGeU64` | 0 | 2 | 1 | ✅ |
| `0xF7` | `AndI32` | 0 | 2 | 1 | ✅ |
| `0xF8` | `OrI32` | 0 | 2 | 1 | ✅ |
| `0xF9` | `XorI32` | 0 | 2 | 1 | ✅ |
| `0xFA` | `ShlI32` | 0 | 2 | 1 | ✅ |
| `0xFB` | `ShrI32` | 0 | 2 | 1 | ✅ |


### Scalar opcode coverage

The opcode table above is the numeric opcode set implemented today. The coverage tables below expand that list across every scalar type supported by the language surface. A checked row means the bytecode has direct opcode coverage for that scalar family. An unchecked row means the scalar is currently lowered through another representation, rejected before bytecode, or still needs dedicated bytecode design.

Scalar set used here:

```txt
i8 i16 i32 i64 i128
u8 u16 u32 u64 u128
f32 f64
bool char
```

#### Scalar constants

| Scalar | Opcode | Status |
|---|---|:---:|
| `i8` | `ConstI8` | ✅ |
| `i16` | `ConstI16` | ✅ |
| `i32` | `ConstI32` | ✅ |
| `i64` | `ConstI64` | ✅ |
| `i128` | `ConstI128` | ✅ |
| `u8` | `ConstU8` | ✅ |
| `u16` | `ConstU16` | ✅ |
| `u32` | `ConstU32` | ✅ |
| `u64` | `ConstU64` | ✅ |
| `u128` | `ConstU128` | ✅ |
| `f32` | `ConstF32` | ✅ |
| `f64` | `ConstF64` | ✅ |
| `bool` | `ConstBool` | ✅ |
| `char` | `ConstChar` | ✅ |

#### Arithmetic opcodes

`Add`, `Sub`, `Mul`, `Div`, and `Mod` are considered complete for integer scalars only when all five operations exist. Floating scalars require `Add`, `Sub`, `Mul`, and `Div`; float modulo is not currently part of the bytecode contract.

| Scalar | Direct arithmetic opcodes | Status |
|---|---|:---:|
| `i8` | — | ☐ |
| `i16` | — | ☐ |
| `i32` | `AddI32`, `SubI32`, `MulI32`, `DivI32`, `ModI32` | ✅ |
| `i64` | `AddI64`, `SubI64`, `MulI64`, `DivI64`, `ModI64` | ✅ |
| `i128` | — | ☐ |
| `u8` | — | ☐ |
| `u16` | — | ☐ |
| `u32` | `AddU32`, `SubU32`, `MulU32`, `DivU32`, `ModU32` | ✅ |
| `u64` | `AddU64`, `SubU64`, `MulU64`, `DivU64`, `ModU64` | ✅ |
| `u128` | — | ☐ |
| `f32` | `AddF32`, `SubF32`, `MulF32`, `DivF32` | ✅ |
| `f64` | `AddF64`, `SubF64`, `MulF64`, `DivF64` | ✅ |
| `bool` | boolean-only, see logical ops | ✅ |
| `char` | — | ☐ |

#### Increment, decrement, and negation

| Scalar | Increment/decrement | Negation | Status |
|---|---|---|:---:|
| `i8` | `IncI8`, `DecI8` | `NegI8` | ✅ |
| `i16` | `IncI16`, `DecI16` | `NegI16` | ✅ |
| `i32` | `IncI32`, `DecI32` | `NegI32` | ✅ |
| `i64` | `IncI64`, `DecI64` | `NegI64` | ✅ |
| `i128` | — | — | ☐ |
| `u8` | `IncU8`, `DecU8` | `NegU8` | ✅ |
| `u16` | `IncU16`, `DecU16` | `NegU16` | ✅ |
| `u32` | `IncU32`, `DecU32` | `NegU32` | ✅ |
| `u64` | `IncU64`, `DecU64` | `NegU64` | ✅ |
| `u128` | — | — | ☐ |
| `f32` | `IncF32`, `DecF32` | `NegF32` | ✅ |
| `f64` | `IncF64`, `DecF64` | `NegF64` | ✅ |
| `bool` | — | — | ☐ |
| `char` | — | — | ☐ |

#### Comparisons

Complete comparison coverage means equality, inequality, less-than, less-or-equal, greater-than, and greater-or-equal opcodes exist for the scalar.

| Scalar | Comparison opcode family | Status |
|---|---|:---:|
| `i8` | — | ☐ |
| `i16` | — | ☐ |
| `i32` | `Cmp*I32` | ✅ |
| `i64` | `Cmp*I64` | ✅ |
| `i128` | — | ☐ |
| `u8` | — | ☐ |
| `u16` | — | ☐ |
| `u32` | `Cmp*U32` | ✅ |
| `u64` | `Cmp*U64` | ✅ |
| `u128` | — | ☐ |
| `f32` | `Cmp*F32` | ✅ |
| `f64` | `Cmp*F64` | ✅ |
| `bool` | no dedicated comparison family; boolean logic exists | ☐ |
| `char` | no dedicated comparison family | ☐ |

#### Bitwise and shifts

| Scalar | Bitwise/shift opcode family | Status |
|---|---|:---:|
| `i8` | — | ☐ |
| `i16` | — | ☐ |
| `i32` | `AndI32`, `OrI32`, `XorI32`, `ShlI32`, `ShrI32` | ✅ |
| `i64` | `AndI64`, `OrI64`, `XorI64`, `ShlI64`, `ShrI64` | ✅ |
| `i128` | — | ☐ |
| `u8` | — | ☐ |
| `u16` | — | ☐ |
| `u32` | uses `I32` bitwise family today | ☐ |
| `u64` | uses `I64` bitwise family today | ☐ |
| `u128` | — | ☐ |
| `bool` | `BoolAnd`, `BoolOr`, `BoolNot` | ✅ |
| `char` | — | ☐ |

#### Conversion opcodes

The conversion family is intentionally incomplete today.

| Conversion group | Direct opcode coverage | Status |
|---|---|:---:|
| `i32 <-> i64` | `ConvI32ToI64`, `ConvI64ToI32` | ✅ |
| `i32 -> f32/f64` | `ConvI32ToF32`, `ConvI32ToF64` | ✅ |
| `f32/f64 -> i32` | `ConvF32ToI32`, `ConvF64ToI32` | ✅ |
| `f32 <-> f64` | `ConvF32ToF64`, `ConvF64ToF32` | ✅ |
| `i8/i16` widening/narrowing | — | ☐ |
| `u8/u16/u32/u64` conversions | — | ☐ |
| `i128/u128` conversions | — | ☐ |
| `i64/u64 <-> f32/f64` | — | ☐ |
| `char` conversions | — | ☐ |
| checked conversions | — | ☐ |

#### Aggregate scalar specialization

Arrays and lists currently specialize only a subset of scalar payloads plus `ref`.

| Payload | Array opcodes | List opcodes | Status |
|---|---|---|:---:|
| `i8` | — | — | ☐ |
| `i16` | — | — | ☐ |
| `i32` | `NewArray`, `ArrayGetI32`, `ArraySetI32` | `NewList`, `ListGetI32`, `ListSetI32`, `ListPushI32`, `ListPopI32`, `ListInsertI32`, `ListRemoveI32` | ✅ |
| `i64` | `NewArrayI64`, `ArrayGetI64`, `ArraySetI64` | `NewListI64`, `ListGetI64`, `ListSetI64`, `ListPushI64`, `ListPopI64`, `ListInsertI64`, `ListRemoveI64` | ✅ |
| `i128` | — | — | ☐ |
| `u8` | — | — | ☐ |
| `u16` | — | — | ☐ |
| `u32` | — | — | ☐ |
| `u64` | — | — | ☐ |
| `u128` | — | — | ☐ |
| `f32` | `NewArrayF32`, `ArrayGetF32`, `ArraySetF32` | `NewListF32`, `ListGetF32`, `ListSetF32`, `ListPushF32`, `ListPopF32`, `ListInsertF32`, `ListRemoveF32` | ✅ |
| `f64` | `NewArrayF64`, `ArrayGetF64`, `ArraySetF64` | `NewListF64`, `ListGetF64`, `ListSetF64`, `ListPushF64`, `ListPopF64`, `ListInsertF64`, `ListRemoveF64` | ✅ |
| `bool` | — | — | ☐ |
| `char` | — | — | ☐ |
| `ref` | `NewArrayRef`, `ArrayGetRef`, `ArraySetRef` | `NewListRef`, `ListGetRef`, `ListSetRef`, `ListPushRef`, `ListPopRef`, `ListInsertRef`, `ListRemoveRef` | ✅ |


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
