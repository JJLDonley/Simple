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

## Binary section schema

SBC metadata rows are compact little-endian POD-style records defined in `Byte/include/sbc_types.h`. Section table entries identify each section by id, byte offset, byte size, and row count.

| Status | Section id | Name | Row/payload | Purpose |
|:---:|---:|---|---|---|
| ✅ | `1` | `Types` | `TypeRow` | type metadata |
| ✅ | `2` | `Fields` | `FieldRow` | object/artifact field metadata |
| ✅ | `3` | `Methods` | `MethodRow` | callable method metadata |
| ✅ | `4` | `Sigs` | `SigRow` plus param type list | function signatures |
| ✅ | `5` | `ConstPool` | tagged variable payloads | constants and strings |
| ✅ | `6` | `Globals` | `GlobalRow` | global slots |
| ✅ | `7` | `Functions` | `FunctionRow` | bytecode ranges and stack limits |
| ✅ | `8` | `Code` | opcode byte stream | executable instructions |
| ✅ | `9` | `Debug` | `DebugHeader` + debug rows | source/debug metadata |
| ✅ | `10` | `Imports` | `ImportRow` | external symbols |
| ✅ | `11` | `Exports` | `ExportRow` | exported functions |
| ✅ | `12` | `Module` | module metadata | module identity |
| ✅ | `13` | `Data` | typed blob rows | immutable data section payload |
| ✅ | `14` | `Capabilities` | capability rows | sandbox/security metadata payload |

## Binary type codes

| Status | Code | TypeKind | SIR spelling | Notes |
|:---:|---:|---|---|---|
| ✅ | `0` | `Unspecified` | internal | placeholder/invalid default |
| ✅ | `1` | `I32` | `i32` | signed integer |
| ✅ | `2` | `I64` | `i64` | signed integer |
| ✅ | `3` | `F32` | `f32` | IEEE-754 binary32 |
| ✅ | `4` | `F64` | `f64` | IEEE-754 binary64 |
| ✅ | `5` | `Ref` | `ref` | heap reference |
| ✅ | `6` | `I8` | `i8` | signed integer |
| ✅ | `7` | `I16` | `i16` | signed integer |
| ✅ | `9` | `U8` | `u8` | unsigned integer |
| ✅ | `10` | `U16` | `u16` | unsigned integer |
| ✅ | `11` | `U32` | `u32` | unsigned integer |
| ✅ | `12` | `U64` | `u64` | unsigned integer |
| ✅ | `14` | `Bool` | `bool` | boolean |
| ✅ | `15` | `Char` | `char` | current 16-bit char payload |
| ✅ | `16` | `String` | `string` | string reference |
| ✅ | `17` | `Void` | `void` | no-result signature/metadata type |
| ✅ | `18` | `Never` | `never` | non-returning metadata type |
| ✅ | `19` | `Ptr` | `ptr<T>` | typed pointer metadata |
| ✅ | `20` | `Array` | `array<T>` | aggregate metadata |
| ✅ | `21` | `List` | `list<T>` | aggregate metadata |
| ✅ | `22` | `Function` | `fn<sig>` | typed function ref metadata |
| ✅ | `23` | `Result` | `result<T,E>` | tagged result metadata |
| ✅ | `24` | `Option` | `option<T>` | optional value metadata |
| ✅ | `25` | `Vector` | `vec<T,N>` | SIMD/vector metadata |

## Binary row schemas

| Status | Row | Fields |
|:---:|---|---|
| ✅ | `SbcHeader` | `magic`, `version`, `endian`, `flags`, `section_count`, `section_table_offset`, `entry_method_id`, reserved words |
| ✅ | `SectionEntry` | `id`, `offset`, `size`, `count` |
| ✅ | `TypeRow` | `name_str`, `kind`, `flags`, `size`, `field_start`, `field_count` |
| ✅ | `FieldRow` | `name_str`, `type_id`, `offset`, `flags` |
| ✅ | `MethodRow` | `name_str`, `sig_id`, `code_offset`, `local_count`, `flags` |
| ✅ | `SigRow` | `ret_type_id`, `param_count`, `call_conv`, `param_type_start` |
| ✅ | `GlobalRow` | `name_str`, `type_id`, `flags`, `init_const_id` |
| ✅ | `FunctionRow` | `method_id`, `code_offset`, `code_size`, `stack_max` |
| ✅ | `ImportRow` | `module_name_str`, `symbol_name_str`, `sig_id`, `flags` |
| ✅ | `ExportRow` | `symbol_name_str`, `func_id`, `flags`, `reserved` |
| ✅ | `DebugHeader` | `file_count`, `line_count`, `sym_count`, `reserved` |
| ✅ | `DebugFileRow` | `file_name_str`, `file_hash` |
| ✅ | `DebugLineRow` | `method_id`, `code_offset`, `file_id`, `line`, `column` |
| ✅ | `DebugSymRow` | `kind`, `owner_id`, `symbol_id`, `name_str` |
| ✅ | planned | module/data/capability raw payload sections |

## SBC constants, imports, and debug contracts

| Status | Area | Binary contract |
|:---:|---|---|
| ✅ | const strings | length-delimited bytes inside const pool; loader checks payload bounds |
| ✅ | numeric constants | encoded as typed payloads where lowering emits const-pool entries |
| ✅ | bytes/data constants | typed blob const-pool rows (`kind=7` bytes, `kind=8` data), reserved for `ConstBytes`, `ConstData`, `LoadDataRef` |
| ✅ | imports | `ImportRow` names module/symbol strings and signature id; method/function metadata marks import callability |
| ✅ | debug lines | debug section rows map method/code offset to file/line/column |
| ✅ | source spans | SIR `span` pseudo-instruction lowers to bytecode `Line` markers; full range metadata remains represented through debug rows |

## SBC versioning and diagnostics

| Status | Area | Contract |
|:---:|---|---|
| ✅ | magic | `SBC0` / `0x30434253` |
| ✅ | version | current binary version `0x0001` |
| ✅ | endian | loader validates header endian marker |
| ✅ | bounds | loader validates section table, rows, const pool, code ranges, and references |
| ☐ | opcode metadata version | planned independent opcode/typed-family compatibility marker |
| ☐ | diagnostic codes | planned stable byte loader/verifier diagnostic code table |

## Opcodes

Opcodes are defined in `Byte/include/opcode.h` with metadata in `Byte/src/opcode.cpp`. The opcode stream is a sequence of opcode bytes plus little-endian immediates.

The tables below are the full robust typed opcode plan grouped by operation family. Concrete scalar families are collapsed into `<T>` rows; assigned implementation opcodes are listed underneath as compact `Code | T` maps instead of duplicating a concrete row and a generic row.

`Operands` is the number of immediate bytes following the opcode byte. `Pops` and `Pushes` are the static stack-effect metadata used by the verifier; call-family opcodes carry dynamic arity in their operands/signatures, so their static stack effect is recorded as `0/0`.

Status values:

- `✅`: fully implemented concrete opcode.
- `◐`: typed family is partially implemented for the listed `Code | T` mappings.
- `☐`: planned opcode family with no assigned opcode yet.

`<T>` means a typed opcode family over the relevant scalar/reference payload set instead of listing every scalar spelling. For numeric scalar families, `<T>` means the valid subset of `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64`; boolean, char, ref, pointer, string, enum, and vector families state their own payload rules.


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
| ✅ | `0x1D` | `ConstU8` | 1 | 0 | 1 |
| ✅ | `0x1E` | `ConstU16` | 2 | 0 | 1 |
| ✅ | `0x1F` | `ConstU32` | 4 | 0 | 1 |
| ✅ | `0x20` | `ConstU64` | 8 | 0 | 1 |
| ✅ | `0x22` | `ConstF32` | 4 | 0 | 1 |
| ✅ | `0x23` | `ConstF64` | 8 | 0 | 1 |
| ✅ | `0x24` | `ConstBool` | 1 | 0 | 1 |
| ✅ | `0x25` | `ConstChar` | 2 | 0 | 1 |
| ✅ | `0x26` | `ConstString` | 4 | 0 | 1 |
| ✅ | `0x27` | `ConstNull` | 0 | 0 | 1 |
| ✅ | pseudo | `ConstBytes` | 4 | 0 | 1 |
| ✅ | pseudo | `ConstData` | 4 | 0 | 1 |
| ✅ | pseudo | `LoadDataRef` | 4 | 0 | 1 |

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
| ✅ | `0x3D` | `InitGlobal` | 4 | 0 | 0 |
| ✅ | `0x3E` | `InitModule` | 4 | 0 | 0 |
| ✅ | `0x3F` | `EnsureModuleInit` | 4 | 0 | 0 |

### Arithmetic

Binary arithmetic. `<T>` covers every valid numeric scalar type.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `Add<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Sub<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Mul<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Div<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Mod<T>` | 0 | 2 | 1 |

Add<T> codes:

| Code | T |
|---:|---|
| `0x40` | `I32` |
| `0x45` | `I64` |
| `0x4A` | `F32` |
| `0x4E` | `F64` |
| `0xE1` | `U32` |
| `0xE6` | `U64` |

Sub<T> codes:

| Code | T |
|---:|---|
| `0x41` | `I32` |
| `0x46` | `I64` |
| `0x4B` | `F32` |
| `0x4F` | `F64` |
| `0xE2` | `U32` |
| `0xE7` | `U64` |

Mul<T> codes:

| Code | T |
|---:|---|
| `0x42` | `I32` |
| `0x47` | `I64` |
| `0x4C` | `F32` |
| `0x5C` | `F64` |
| `0xE3` | `U32` |
| `0xE8` | `U64` |

Div<T> codes:

| Code | T |
|---:|---|
| `0x43` | `I32` |
| `0x48` | `I64` |
| `0x4D` | `F32` |
| `0x5D` | `F64` |
| `0xE4` | `U32` |
| `0xE9` | `U64` |

Mod<T> codes:

| Code | T |
|---:|---|
| `0x44` | `I32` |
| `0x49` | `I64` |
| `0xE5` | `U32` |
| `0xEA` | `U64` |


### Increment and decrement

Unary increment/decrement over valid numeric scalar types.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `Inc<T>` | 0 | 1 | 1 |
| ◐ | `typed` | `Dec<T>` | 0 | 1 | 1 |

Inc<T> codes:

| Code | T |
|---:|---|
| `0x92` | `I8` |
| `0x94` | `I16` |
| `0x83` | `I32` |
| `0x85` | `I64` |
| `0x96` | `U8` |
| `0x98` | `U16` |
| `0x8B` | `U32` |
| `0x8D` | `U64` |
| `0x87` | `F32` |
| `0x89` | `F64` |

Dec<T> codes:

| Code | T |
|---:|---|
| `0x93` | `I8` |
| `0x95` | `I16` |
| `0x84` | `I32` |
| `0x86` | `I64` |
| `0x97` | `U8` |
| `0x99` | `U16` |
| `0x8C` | `U32` |
| `0x8E` | `U64` |
| `0x88` | `F32` |
| `0x8A` | `F64` |


### Negation

Unary numeric negation over valid numeric scalar types.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `Neg<T>` | 0 | 1 | 1 |

Neg<T> codes:

| Code | T |
|---:|---|
| `0x9A` | `I8` |
| `0x9B` | `I16` |
| `0x5E` | `I32` |
| `0x5F` | `I64` |
| `0x9C` | `U8` |
| `0x9D` | `U16` |
| `0x9E` | `U32` |
| `0x9F` | `U64` |
| `0x7E` | `F32` |
| `0x7F` | `F64` |


### Comparisons

Equality and ordering comparisons over comparable typed values.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `CmpEq<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `CmpNe<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `CmpLt<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `CmpLe<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `CmpGt<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `CmpGe<T>` | 0 | 2 | 1 |

CmpEq<T> codes:

| Code | T |
|---:|---|
| `0x50` | `I32` |
| `0x56` | `I64` |
| `0x63` | `F32` |
| `0x69` | `F64` |
| `0xEB` | `U32` |
| `0xF1` | `U64` |

CmpNe<T> codes:

| Code | T |
|---:|---|
| `0x52` | `I32` |
| `0x57` | `I64` |
| `0x64` | `F32` |
| `0x6A` | `F64` |
| `0xEC` | `U32` |
| `0xF2` | `U64` |

CmpLt<T> codes:

| Code | T |
|---:|---|
| `0x51` | `I32` |
| `0x58` | `I64` |
| `0x65` | `F32` |
| `0x6B` | `F64` |
| `0xED` | `U32` |
| `0xF3` | `U64` |

CmpLe<T> codes:

| Code | T |
|---:|---|
| `0x53` | `I32` |
| `0x59` | `I64` |
| `0x66` | `F32` |
| `0x6C` | `F64` |
| `0xEE` | `U32` |
| `0xF4` | `U64` |

CmpGt<T> codes:

| Code | T |
|---:|---|
| `0x54` | `I32` |
| `0x5A` | `I64` |
| `0x67` | `F32` |
| `0x6D` | `F64` |
| `0xEF` | `U32` |
| `0xF5` | `U64` |

CmpGe<T> codes:

| Code | T |
|---:|---|
| `0x55` | `I32` |
| `0x5B` | `I64` |
| `0x68` | `F32` |
| `0x6E` | `F64` |
| `0xF0` | `U32` |
| `0xF6` | `U64` |


### Boolean logic

Boolean logical operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x60` | `BoolNot` | 0 | 1 | 1 |
| ✅ | `0x61` | `BoolAnd` | 0 | 2 | 1 |
| ✅ | `0x62` | `BoolOr` | 0 | 2 | 1 |

### Bitwise and shifts

Bitwise and shift operations over integer scalar types.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `And<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Or<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Xor<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Shl<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `Shr<T>` | 0 | 2 | 1 |

And<T> codes:

| Code | T |
|---:|---|
| `0xF7` | `I32` |
| `0xD4` | `I64` |

Or<T> codes:

| Code | T |
|---:|---|
| `0xF8` | `I32` |
| `0xD5` | `I64` |

Xor<T> codes:

| Code | T |
|---:|---|
| `0xF9` | `I32` |
| `0xD6` | `I64` |

Shl<T> codes:

| Code | T |
|---:|---|
| `0xFA` | `I32` |
| `0xD7` | `I64` |

Shr<T> codes:

| Code | T |
|---:|---|
| `0xFB` | `I32` |
| `0xD8` | `I64` |


### Calls

Direct, indirect, tail, import/native, method, and virtual call forms. Typed calls reference signature metadata.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x70` | `Call` | 5 | 0 | 0 |
| ✅ | `0x71` | `CallIndirect` | 5 | 0 | 0 |
| ✅ | `0x72` | `TailCall` | 5 | 0 | 0 |
| ✅ | `0xE0` | `CallCheck` | 0 | 0 | 0 |
| ✅ | `0xFE` | `CallImport` | 5 | 0 | 0 |
| ✅ | `0xFF` | `CallNative` | 5 | 0 | 0 |
| ✅ | pseudo | `CallMethod` | 5 | 0 | 0 |
| ✅ | pseudo | `CallVirtual` | 5 | 0 | 0 |

### Conversions

Scalar conversions. `<From,To>` covers explicit typed conversion pairs. Checked variants trap on invalid/narrowing overflow.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `Conv<From,To>` | 0 | 1 | 1 |
| ✅ | pseudo | `CheckedConv<From,To>` | 0 | 1 | 1 |

Conv<From,To> codes:

| Code | T |
|---:|---|
| `0x76` | `I32 -> I64` |
| `0x77` | `I64 -> I32` |
| `0x78` | `I32 -> F32` |
| `0x79` | `I32 -> F64` |
| `0x7A` | `F32 -> I32` |
| `0x7B` | `F64 -> I32` |
| `0x7C` | `F32 -> F64` |
| `0x7D` | `F64 -> F32` |


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
| ✅ | `0x29` | `TraceEnter` | 4 | 0 | 0 |
| ✅ | `0x2A` | `TraceLeave` | 4 | 0 | 0 |
| ✅ | `0x28` | `StackTrace` | 0 | 0 | 1 |

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
| ✅ | pseudo | `CaptureLocal` | 4 | 0 | 1 |
| ✅ | pseudo | `CaptureRef` | 4 | 0 | 1 |
| ✅ | pseudo | `CloseUpvalue` | 4 | 0 | 0 |
| ✅ | pseudo | `InitObject` | 4 | 1 | 1 |
| ✅ | `0x2D` | `DropObject` | 0 | 1 | 0 |
| ✅ | `0x2E` | `CloneObject` | 0 | 1 | 1 |
| ✅ | `0x2F` | `ObjectEq` | 0 | 2 | 1 |
| ✅ | pseudo | `InstanceOf<T>` | 4 | 1 | 1 |
| ✅ | pseudo | `CastRef<T>` | 4 | 1 | 1 |
| ✅ | pseudo | `CheckedCastRef<T>` | 4 | 1 | 1 |
| ✅ | pseudo | `LoadVTable` | 0 | 1 | 1 |

### Arrays

Fixed-size array allocation and element access. `<T>` covers every scalar payload plus `ref`.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `NewArray<T>` | 8 | 0 | 1 |
| ◐ | `typed` | `ArrayGet<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `ArraySet<T>` | 0 | 3 | 0 |
| ✅ | `0x17` | `ArrayCopy<T>` | 0 | 5 | 0 |
| ✅ | `0x8F` | `ArrayFill<T>` | 0 | 3 | 0 |

NewArray<T> codes:

| Code | T |
|---:|---|
| `0xB0` | `generic` |
| `0xB4` | `I64` |
| `0xB7` | `F32` |
| `0xBA` | `F64` |
| `0xBD` | `Ref` |

ArrayGet<T> codes:

| Code | T |
|---:|---|
| `0xB2` | `I32` |
| `0xB5` | `I64` |
| `0xB8` | `F32` |
| `0xBB` | `F64` |
| `0xBE` | `Ref` |

ArraySet<T> codes:

| Code | T |
|---:|---|
| `0xB3` | `I32` |
| `0xB6` | `I64` |
| `0xB9` | `F32` |
| `0xBC` | `F64` |
| `0xBF` | `Ref` |


### Lists

Growable list allocation and element operations. `<T>` covers every scalar payload plus `ref`.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ◐ | `typed` | `NewList<T>` | 8 | 0 | 1 |
| ◐ | `typed` | `ListGet<T>` | 0 | 2 | 1 |
| ◐ | `typed` | `ListSet<T>` | 0 | 3 | 0 |
| ◐ | `typed` | `ListPush<T>` | 0 | 2 | 0 |
| ◐ | `typed` | `ListPop<T>` | 0 | 1 | 1 |
| ◐ | `typed` | `ListInsert<T>` | 0 | 3 | 0 |
| ◐ | `typed` | `ListRemove<T>` | 0 | 2 | 1 |
| ✅ | `0xC1` | `ListLen` | 0 | 1 | 1 |
| ✅ | `0xC8` | `ListClear` | 0 | 1 | 0 |
| ✅ | `0xAF` | `ListReserve` | 0 | 2 | 0 |
| ✅ | `0x6F` | `ListResize` | 0 | 3 | 0 |

NewList<T> codes:

| Code | T |
|---:|---|
| `0x36` | `Ref` |
| `0xA8` | `F64` |
| `0xC0` | `generic` |
| `0xC9` | `F32` |
| `0xD9` | `I64` |

ListGet<T> codes:

| Code | T |
|---:|---|
| `0x37` | `Ref` |
| `0xA9` | `F64` |
| `0xC2` | `I32` |
| `0xCA` | `F32` |
| `0xDA` | `I64` |

ListSet<T> codes:

| Code | T |
|---:|---|
| `0x38` | `Ref` |
| `0xAA` | `F64` |
| `0xC3` | `I32` |
| `0xCB` | `F32` |
| `0xDB` | `I64` |

ListPush<T> codes:

| Code | T |
|---:|---|
| `0x39` | `Ref` |
| `0xAB` | `F64` |
| `0xC4` | `I32` |
| `0xCC` | `F32` |
| `0xDC` | `I64` |

ListPop<T> codes:

| Code | T |
|---:|---|
| `0x3A` | `Ref` |
| `0xAC` | `F64` |
| `0xC5` | `I32` |
| `0xCD` | `F32` |
| `0xDD` | `I64` |

ListInsert<T> codes:

| Code | T |
|---:|---|
| `0x3B` | `Ref` |
| `0xAD` | `F64` |
| `0xC6` | `I32` |
| `0xCE` | `F32` |
| `0xDE` | `I64` |

ListRemove<T> codes:

| Code | T |
|---:|---|
| `0x3C` | `Ref` |
| `0xAE` | `F64` |
| `0xC7` | `I32` |
| `0xCF` | `F32` |
| `0xDF` | `I64` |


### Strings

String-specific operations.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0xD0` | `StringLen` | 0 | 1 | 1 |
| ✅ | `0xD1` | `StringConcat` | 0 | 2 | 1 |
| ✅ | `0xFC` | `StringEq` | 0 | 2 | 1 |
| ✅ | `0xFD` | `StringNe` | 0 | 2 | 1 |
| ✅ | `0xD2` | `StringGetChar` | 0 | 2 | 1 |
| ✅ | `0xD3` | `StringSlice` | 0 | 3 | 1 |
| ☐ | `TBD` | `StringEq` | 0 | 2 | 1 |
| ☐ | `TBD` | `StringNe` | 0 | 2 | 1 |
| ✅ | `0x15` | `StringCompare` | 0 | 2 | 1 |
| ✅ | `0x16` | `StringFind` | 0 | 2 | 1 |
| ☐ | `TBD` | `StringToBytes` | 0 | 1 | 1 |
| ☐ | `TBD` | `BytesToString` | 0 | 1 | 1 |

### Pointer and memory

Explicit pointer/address and raw memory operations. `<T>` load/store rows are typed by pointee/value type.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | pseudo | `AddressOfLocal` | 4 | 0 | 1 |
| ✅ | pseudo | `AddressOfGlobal` | 4 | 0 | 1 |
| ✅ | pseudo | `AddressOfField` | 4 | 1 | 1 |
| ☐ | `TBD` | `LoadPtr<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `StorePtr<T>` | 0 | 2 | 0 |
| ☐ | `TBD` | `PtrAdd` | 0 | 2 | 1 |
| ☐ | `TBD` | `PtrOffset` | 0 | 2 | 1 |
| ✅ | pseudo | `PtrEq` | 0 | 2 | 1 |
| ✅ | pseudo | `PtrNe` | 0 | 2 | 1 |
| ✅ | pseudo | `PtrIsNull` | 0 | 1 | 1 |
| ✅ | pseudo | `PtrCheckNull` | 0 | 1 | 1 |
| ✅ | pseudo | `PtrCheckBounds` | 0 | 3 | 1 |
| ☐ | `TBD` | `MemCopy` | 0 | 3 | 0 |
| ☐ | `TBD` | `MemMove` | 0 | 3 | 0 |
| ☐ | `TBD` | `MemSet` | 0 | 3 | 0 |
| ☐ | `TBD` | `MemCompare` | 0 | 3 | 1 |

### Checked operations

Checked arithmetic, bounds, null, and conversion operations. `<T>` covers numeric scalar types valid for the operation.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | pseudo | `CheckedAdd<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedSub<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedMul<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedDiv<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedMod<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedArrayGet<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedArraySet<T>` | 0 | 3 | 0 |
| ✅ | pseudo | `CheckedListGet<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedListSet<T>` | 0 | 3 | 0 |
| ✅ | pseudo | `CheckedStringGetChar` | 0 | 2 | 1 |
| ✅ | pseudo | `CheckedStringSlice` | 0 | 3 | 1 |
| ✅ | `0x2B` | `CheckedNull` | 0 | 1 | 1 |
| ✅ | `0x2C` | `CheckedBounds` | 0 | 3 | 1 |

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
| ✅ | pseudo | `Throw` | 0 | 1 | 0 |
| ✅ | pseudo | `Catch` | 4 | 0 | 0 |
| ✅ | pseudo | `Finally` | 4 | 0 | 0 |
| ✅ | pseudo | `Panic` | 0 | 1 | 0 |

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
| ✅ | pseudo | `Spawn` | 4 | 0 | 1 |
| ✅ | pseudo | `Join` | 0 | 1 | 1 |
| ✅ | pseudo | `Detach` | 0 | 1 | 0 |
| ✅ | pseudo | `Await` | 0 | 1 | 1 |
| ✅ | `0x0E` | `Yield` | 0 | 0 | 0 |
| ✅ | pseudo | `Resume` | 0 | 1 | 0 |
| ✅ | pseudo | `Suspend` | 0 | 0 | 1 |
| ✅ | pseudo | `MakeFuture` | 4 | 0 | 1 |
| ✅ | pseudo | `PollFuture` | 0 | 1 | 2 |
| ☐ | `TBD` | `ChannelSend<T>` | 0 | 2 | 0 |
| ☐ | `TBD` | `ChannelRecv<T>` | 0 | 1 | 1 |
| ☐ | `TBD` | `ChannelTryRecv<T>` | 0 | 1 | 2 |
| ✅ | pseudo | `AtomicLoad<T>` | 0 | 1 | 1 |
| ✅ | pseudo | `AtomicStore<T>` | 0 | 2 | 0 |
| ✅ | pseudo | `AtomicAdd<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `AtomicSub<T>` | 0 | 2 | 1 |
| ✅ | pseudo | `AtomicCompareExchange<T>` | 0 | 3 | 1 |
| ✅ | `0x0F` | `Fence` | 0 | 0 | 0 |

### Locks and monitors

Monitor/lock operations for VM-managed synchronization if needed.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | pseudo | `Lock` | 0 | 1 | 0 |
| ✅ | pseudo | `Unlock` | 0 | 1 | 0 |
| ✅ | pseudo | `TryLock` | 0 | 1 | 1 |
| ✅ | pseudo | `Wait` | 0 | 1 | 0 |
| ✅ | pseudo | `Notify` | 0 | 1 | 0 |
| ✅ | pseudo | `NotifyAll` | 0 | 1 | 0 |

### GC and runtime barriers

GC/runtime coordination opcodes.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x08` | `Safepoint` | 0 | 0 | 0 |
| ✅ | `0x09` | `AllocCheckpoint` | 0 | 0 | 0 |
| ✅ | pseudo | `WriteBarrier` | 0 | 2 | 0 |
| ✅ | pseudo | `ReadBarrier` | 0 | 1 | 1 |
| ✅ | pseudo | `PinRef` | 0 | 1 | 1 |
| ✅ | pseudo | `UnpinRef` | 0 | 1 | 0 |
| ✅ | `0x0A` | `KeepAlive` | 0 | 1 | 0 |

### JIT and deoptimization

JIT patching, guards, and deoptimization hooks for a typed optimizing backend.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | pseudo | `Deopt` | 4 | 0 | 0 |
| ✅ | pseudo | `Patchpoint` | 4 | 0 | 0 |
| ✅ | pseudo | `InlineCache` | 4 | 0 | 0 |
| ✅ | pseudo | `GuardType<T>` | 4 | 1 | 1 |
| ✅ | pseudo | `GuardBounds` | 0 | 3 | 1 |
| ✅ | pseudo | `GuardNotNull` | 0 | 1 | 1 |

### Capabilities and sandboxing

Optional capability/security checks for restricted runtimes.

| Status | Value | Name | Operands | Pops | Pushes |
|:---:|---:|---|---:|---:|---:|
| ✅ | `0x0B` | `CheckCapability` | 4 | 0 | 0 |
| ✅ | `0x0C` | `EnterSandbox` | 4 | 0 | 0 |
| ✅ | `0x0D` | `ExitSandbox` | 0 | 0 | 0 |

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
