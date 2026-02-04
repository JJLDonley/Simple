# Simple::Byte

**Scope**  
SBC file format, encoding, metadata tables, opcode list, loader rules, and verifier rules.

**How It Works**  
The Byte module defines the SBC binary layout and validation rules. The loader parses headers,
sections, tables, heaps, and code; the verifier enforces structural and type safety before run/JIT.

**What Works (Current)**  
- Headers/sections/encoding rules implemented and validated.  
- Metadata tables + heaps decoded with strict bounds checks.  
- Verifier enforces stack/type safety and control-flow validity.  

**Implementation Plan**  
See `Simple/Docs/Implementation.md` (Module: Simple::Byte).

---

## Specification (Legacy Reference, Organized)

### Headers

# SBC Headers (Design Requirements)

This document defines the exact requirements for the Simple Bytecode (SBC) file header.

---

## 1. Header Size and Endianness

- Header is **exactly 32 bytes**.
- All multi-byte fields are **little-endian**.
- Header must be located at file offset `0`.

---

## 2. Header Layout (Fixed)

```
Offset Size Field
0x00   4    magic
0x04   2    version
0x06   1    endian
0x07   1    flags
0x08   4    section_count
0x0C   4    section_table_offset
0x10   4    entry_method_id
0x14   4    reserved0
0x18   4    reserved1
0x1C   4    reserved2
```

This layout is **frozen** for v0.1.

---

## 3. Field Requirements

### magic (u32)
- Must be `0x30434253` (ASCII "SBC0").
- Loader must reject any other value.

### version (u16)
- Must be `0x0001` for v0.1.
- Loader must reject unsupported versions.

### endian (u8)
- Must be `1` for little-endian.
- Loader must reject any other value in v0.1.

### flags (u8)
- Bit 0: `has_debug` (DEBUG section present or expected)
- Bit 1: `verified` (module already verified)
- Bit 2: `jit_hint` (JIT preferred)
- Bits 3-7: **reserved**, must be `0`.

### section_count (u32)
- Number of entries in section table.
- Must be > 0.

### section_table_offset (u32)
- File offset to the section table.
- Must be within file bounds.
- Must be aligned to 4 bytes.

### entry_method_id (u32)
- Index into METHOD table.
- Must be valid if any code exists.
- Can be `0xFFFFFFFF` if module is a library with no entry point.

### reserved0/reserved1/reserved2 (u32)
- Must be `0`.
- Loader must reject non-zero in v0.1.

---

## 4. Validation Rules

- Header size must be 32 bytes; file length must be >= 32 bytes.
- All fields must satisfy the constraints above before any section parsing.
- If `has_debug` is set but DEBUG section is missing, loader should warn but continue.

---

## 5. Versioning Policy

- Any change to header layout increments `version`.
- Backward compatibility is optional but must be explicit in loader.


---

### Encoding

# SBC Encoding (Design Requirements)

This document defines the exact bytecode encoding rules for SBC modules.

---

## 1. Endianness

- All multi-byte fields are **little-endian**.

---

## 2. Instruction Encoding

- Instruction format is **opcode (u8)** followed by **zero or more operands**.
- Operand sizes are fixed per opcode; there is **no varint encoding** in v0.1.
- Instructions are densely packed; there is **no padding** between instructions.

---

## 3. Operand Types

- `u8`, `u16`, `u32`, `u64`
- `i32` (signed, relative jump offset)
- `idx` (alias of `u32`, index into a metadata table)

---

## 3.1 VM Type IDs (Frozen v0.1)

These IDs are used in the Type table and signature param type lists.

```
0 = unspecified
1 = i32
2 = i64
3 = f32
4 = f64
5 = ref
```

Rules:
- Value kinds (`i32/i64/f32/f64`) must have matching sizes (4/8).
- `ref` allows size `0/4/8` and must have no fields.

---

## 4. Jump Targets

- All jumps use **signed `i32` byte offsets**.
- Offsets are **relative to the next instruction** (PC after reading operands).
- Targets must land on instruction boundaries.

---

## 5. Constant Encoding

- Constants may be immediate (`CONST_I32 u32`, `CONST_F64 u64`, etc.) or pool-backed (`CONST_I128 idx`, `CONST_STRING idx`).
- The constant pool contains typed entries (see `SBC_Metadata_Tables.md`).

---

## 6. Alignment

- Sections in the module are aligned to 4 bytes.
- No alignment is required inside CODE except instruction natural sizes.

---

## 7. Stack Effects

- Every opcode has a fixed stack effect based on its type suffix (e.g., `ADD_I32` pops 2 x `i32`, pushes 1 x `i32`).
- Stack effects must be verified (see `SBC_Rules.md`).


---

### Sections

# SBC Sections (Design Requirements)

This document defines the section table and required sections in SBC modules.

---

## 1. Section Table

- Section table location is given by `section_table_offset` in the header.
- Each entry is **16 bytes**:

```
struct SectionEntry {
  u32 id;
  u32 offset;
  u32 size;
  u32 count; // row count for tables, 0 for raw sections
}
```

- `offset` and `size` must be within file bounds.
- Section ranges must not overlap.

---

## 2. Section IDs (v0.1)

```
1 = TYPES
2 = FIELDS
3 = METHODS
4 = SIGS
5 = CONST_POOL
6 = GLOBALS
7 = FUNCTIONS
8 = CODE
9 = DEBUG
10 = IMPORTS
11 = EXPORTS
```

Unknown IDs are **rejected** in v0.1.
Section IDs are **frozen** for v0.1.

---

## 3. Required Sections

- `CODE` is required if any function exists.
- `TYPES`, `FIELDS`, `METHODS`, `SIGS`, `CONST_POOL`, `GLOBALS`, `FUNCTIONS` are required for standard modules.
- `DEBUG` is optional.
- `IMPORTS`/`EXPORTS` are optional.

---

## 4. Section Alignment

- All section offsets must be aligned to **4 bytes**.

---

## 5. Section Count

- `section_count` must match the number of entries in the section table exactly.


---

### Metadata Tables

# SBC Metadata Tables (Design Requirements)

This document defines the exact formats for metadata tables and heaps in SBC.

---

## 1. String Heap

- UTF-8, null-terminated strings.
- Offset `0` is a valid empty string.
- All `name_str` fields are offsets into this heap.

## 2. Blob Heap

- Each blob is length-prefixed: `u32 length` followed by raw bytes.
- Offsets point to the length field.

---

## 3. Type Table (TYPES)

### VM Type IDs (Frozen v0.1)

```
0 = unspecified
1 = i32
2 = i64
3 = f32
4 = f64
5 = ref
```

```
struct TypeRow {
  u32 name_str;
  u8  kind;        // 0=unspecified,1=i32,2=i64,3=f32,4=f64,5=ref
  u8  flags;       // bit0=ref_type, bit1=generic, bit2=sealed
  u16 reserved;
  u32 size;        // bytes for value types, 0/4/8 for ref types
  u32 field_start; // index into Field table
  u32 field_count;
}
```

Requirements:
- `field_start + field_count` must be within the Field table.
- `kind` must be one of the VM primitive type kinds above.
- `kind=i32` requires `size=4`; `kind=i64` requires `size=8`.
- `kind=f32` requires `size=4`; `kind=f64` requires `size=8`.
- `kind=ref` allows `size=0/4/8` and must have `field_start=0` and `field_count=0`.
- Value kinds (`i32/i64/f32/f64`) must have `field_start=0` and `field_count=0`.

---

## 4. Field Table (FIELDS)

```
struct FieldRow {
  u32 name_str;
  u32 type_id;
  u32 offset;
  u32 flags;   // bit0=mutable, bit1=static
}
```

Requirements:
- `type_id` must reference a valid Type row.
- `offset` must be within the instance size for value types.

---

## 5. Method Table (METHODS)

```
struct MethodRow {
  u32 name_str;
  u32 sig_id;
  u32 code_offset; // offset into CODE
  u16 local_count;
  u16 flags;       // bit0=static, bit1=instance, bit2=virtual
}
```

Requirements:
- `sig_id` must reference a valid Signature row.
- `code_offset` must be within CODE bounds.

---

## 6. Signature Table (SIGS)

```
struct SigRow {
  u32 ret_type_id;
  u16 param_count;
  u16 call_conv;       // 0=default, 1=varargs
  u32 param_type_start;
}
```

Param Type List (packed):
- `ret_type_id` must reference a valid Type row, or be `0xFFFFFFFF` for void.
- `param_type_start` is an index into a packed `u32` array of type IDs.
- `param_count` entries follow.

---

## 7. Constant Pool (CONST_POOL)

Entry format: `u32 kind` + payload.

```
0 = STRING : u32 str_offset
1 = I128   : u32 blob_offset
2 = U128   : u32 blob_offset
3 = F32    : u32 bits
4 = F64    : u64 bits
5 = TYPE   : u32 type_id
6 = JMP_TABLE : u32 blob_offset (u32 count + i32 offsets)
```

Requirements:
- Offsets must be within heap bounds.
- Const pool formats are **frozen** in v0.1.
- STRING: `str_offset` must point to a null-terminated UTF-8 string.
- I128/U128: `blob_offset` must point to a blob with `length == 16`.
- F32/F64: `bits` are raw IEEE-754 bit patterns.
- JMP_TABLE: `blob_offset` must point to `u32 count` followed by `count` `i32` offsets.

---

## 8. Globals Table (GLOBALS)

```
struct GlobalRow {
  u32 name_str;
  u32 type_id;
  u32 flags;         // bit0=mutable
  u32 init_const_id; // const pool id or 0xFFFFFFFF for zero-init
}
```

---

## 9. Functions Table (FUNCTIONS)

```
struct FunctionRow {
  u32 method_id;
  u32 code_offset;
  u32 code_size;
  u32 stack_max;
}
```

Requirements:
- `method_id` must reference a valid Method row.
- `code_offset + code_size` must be within CODE bounds.

---

## 10. Import Table (IMPORTS)

```
struct ImportRow {
  u32 module_name_str;
  u32 symbol_name_str;
  u32 sig_id;
  u32 flags;
}
```

Requirements:
- `module_name_str` and `symbol_name_str` must be valid CONST_POOL string offsets.
- `sig_id` must reference a valid Signature row.
- `flags` must only use bits 0..3.
- Duplicate `module_name_str + symbol_name_str` pairs are invalid.

---

## 11. Export Table (EXPORTS)

```
struct ExportRow {
  u32 symbol_name_str;
  u32 func_id;
  u32 flags;
  u32 reserved;
}
```

Requirements:
- `symbol_name_str` must be a valid CONST_POOL string offset.
- `func_id` must reference a valid Function row.
- `flags` must only use bits 0..3.
- `reserved` must be 0.
- Duplicate export names are invalid.


---

### OpCodes

# SBC OpCodes (Design Requirements)

This document defines the opcode set for Simple VM bytecode (v0.1).

---

## 1. Opcode Encoding

- Each opcode is a single **u8**.
- Operands are fixed-size and defined per opcode.
- Typed opcodes use explicit suffixes (e.g., `ADD_I32`).
- Operand widths and stack effects are **frozen** in v0.1 and must match the VM `OpInfo` table.
- Unknown opcodes or malformed operand lengths are rejected at load time.

---

## 1.1 Opcode IDs (Frozen v0.1)

Source of truth: `SimpleByteCode/vm/include/opcode.h` (kept in sync with this table).

| Opcode | ID |
|--------|----|
| NOP | 0x00 |
| HALT | 0x01 |
| TRAP | 0x02 |
| BREAKPOINT | 0x03 |
| JMP | 0x04 |
| JMP_TRUE | 0x05 |
| JMP_FALSE | 0x06 |
| JMP_TABLE | 0x07 |
| POP | 0x10 |
| DUP | 0x11 |
| DUP2 | 0x12 |
| SWAP | 0x13 |
| ROT | 0x14 |
| CONST_I8 | 0x18 |
| CONST_I16 | 0x19 |
| CONST_I32 | 0x1A |
| CONST_I64 | 0x1B |
| CONST_I128 | 0x1C |
| CONST_U8 | 0x1D |
| CONST_U16 | 0x1E |
| CONST_U32 | 0x1F |
| CONST_U64 | 0x20 |
| CONST_U128 | 0x21 |
| CONST_F32 | 0x22 |
| CONST_F64 | 0x23 |
| CONST_BOOL | 0x24 |
| CONST_CHAR | 0x25 |
| CONST_STRING | 0x26 |
| CONST_NULL | 0x27 |
| LOAD_LOCAL | 0x30 |
| STORE_LOCAL | 0x31 |
| LOAD_GLOBAL | 0x32 |
| STORE_GLOBAL | 0x33 |
| LOAD_UPVALUE | 0x34 |
| STORE_UPVALUE | 0x35 |
| NEW_LIST_REF | 0x36 |
| LIST_GET_REF | 0x37 |
| LIST_SET_REF | 0x38 |
| LIST_PUSH_REF | 0x39 |
| LIST_POP_REF | 0x3A |
| LIST_INSERT_REF | 0x3B |
| LIST_REMOVE_REF | 0x3C |
| ADD_I32 | 0x40 |
| SUB_I32 | 0x41 |
| MUL_I32 | 0x42 |
| DIV_I32 | 0x43 |
| MOD_I32 | 0x44 |
| ADD_I64 | 0x45 |
| SUB_I64 | 0x46 |
| MUL_I64 | 0x47 |
| DIV_I64 | 0x48 |
| MOD_I64 | 0x49 |
| ADD_F32 | 0x4A |
| SUB_F32 | 0x4B |
| MUL_F32 | 0x4C |
| DIV_F32 | 0x4D |
| ADD_F64 | 0x4E |
| SUB_F64 | 0x4F |
| MUL_F64 | 0x5C |
| DIV_F64 | 0x5D |
| NEG_I32 | 0x5E |
| NEG_I64 | 0x5F |
| INC_I32 | 0x83 |
| DEC_I32 | 0x84 |
| INC_I64 | 0x85 |
| DEC_I64 | 0x86 |
| INC_F32 | 0x87 |
| DEC_F32 | 0x88 |
| INC_F64 | 0x89 |
| DEC_F64 | 0x8A |
| INC_U32 | 0x8B |
| DEC_U32 | 0x8C |
| INC_U64 | 0x8D |
| DEC_U64 | 0x8E |
| INC_I8 | 0x92 |
| DEC_I8 | 0x93 |
| INC_I16 | 0x94 |
| DEC_I16 | 0x95 |
| INC_U8 | 0x96 |
| DEC_U8 | 0x97 |
| INC_U16 | 0x98 |
| DEC_U16 | 0x99 |
| NEG_I8 | 0x9A |
| NEG_I16 | 0x9B |
| NEG_U8 | 0x9C |
| NEG_U16 | 0x9D |
| NEG_U32 | 0x9E |
| NEG_U64 | 0x9F |
| NEG_F32 | 0x7E |
| NEG_F64 | 0x7F |
| CMP_EQ_I32 | 0x50 |
| CMP_LT_I32 | 0x51 |
| CMP_NE_I32 | 0x52 |
| CMP_LE_I32 | 0x53 |
| CMP_GT_I32 | 0x54 |
| CMP_GE_I32 | 0x55 |
| CMP_EQ_I64 | 0x56 |
| CMP_NE_I64 | 0x57 |
| CMP_LT_I64 | 0x58 |
| CMP_LE_I64 | 0x59 |
| CMP_GT_I64 | 0x5A |
| CMP_GE_I64 | 0x5B |
| CMP_EQ_F32 | 0x63 |
| CMP_NE_F32 | 0x64 |
| CMP_LT_F32 | 0x65 |
| CMP_LE_F32 | 0x66 |
| CMP_GT_F32 | 0x67 |
| CMP_GE_F32 | 0x68 |
| CMP_EQ_F64 | 0x69 |
| CMP_NE_F64 | 0x6A |
| CMP_LT_F64 | 0x6B |
| CMP_LE_F64 | 0x6C |
| CMP_GT_F64 | 0x6D |
| CMP_GE_F64 | 0x6E |
| BOOL_NOT | 0x60 |
| BOOL_AND | 0x61 |
| BOOL_OR | 0x62 |
| CALL | 0x70 |
| CALL_INDIRECT | 0x71 |
| TAIL_CALL | 0x72 |
| RET | 0x73 |
| ENTER | 0x74 |
| LEAVE | 0x75 |
| CONV_I32_TO_I64 | 0x76 |
| CONV_I64_TO_I32 | 0x77 |
| CONV_I32_TO_F32 | 0x78 |
| CONV_I32_TO_F64 | 0x79 |
| CONV_F32_TO_I32 | 0x7A |
| CONV_F64_TO_I32 | 0x7B |
| CONV_F32_TO_F64 | 0x7C |
| CONV_F64_TO_F32 | 0x7D |
| LINE | 0x80 |
| PROFILE_START | 0x81 |
| PROFILE_END | 0x82 |
| INTRINSIC | 0x90 |
| SYS_CALL | 0x91 |
| NEW_OBJECT | 0xA0 |
| NEW_CLOSURE | 0xA1 |
| LOAD_FIELD | 0xA2 |
| STORE_FIELD | 0xA3 |
| IS_NULL | 0xA4 |
| REF_EQ | 0xA5 |
| REF_NE | 0xA6 |
| TYPE_OF | 0xA7 |
| NEW_LIST_F64 | 0xA8 |
| LIST_GET_F64 | 0xA9 |
| LIST_SET_F64 | 0xAA |
| LIST_PUSH_F64 | 0xAB |
| LIST_POP_F64 | 0xAC |
| LIST_INSERT_F64 | 0xAD |
| LIST_REMOVE_F64 | 0xAE |
| NEW_ARRAY | 0xB0 |
| ARRAY_LEN | 0xB1 |
| ARRAY_GET_I32 | 0xB2 |
| ARRAY_SET_I32 | 0xB3 |
| NEW_ARRAY_I64 | 0xB4 |
| ARRAY_GET_I64 | 0xB5 |
| ARRAY_SET_I64 | 0xB6 |
| NEW_ARRAY_F32 | 0xB7 |
| ARRAY_GET_F32 | 0xB8 |
| ARRAY_SET_F32 | 0xB9 |
| NEW_ARRAY_F64 | 0xBA |
| ARRAY_GET_F64 | 0xBB |
| ARRAY_SET_F64 | 0xBC |
| NEW_ARRAY_REF | 0xBD |
| ARRAY_GET_REF | 0xBE |
| ARRAY_SET_REF | 0xBF |
| NEW_LIST | 0xC0 |
| LIST_LEN | 0xC1 |
| LIST_GET_I32 | 0xC2 |
| LIST_SET_I32 | 0xC3 |
| LIST_PUSH_I32 | 0xC4 |
| LIST_POP_I32 | 0xC5 |
| LIST_INSERT_I32 | 0xC6 |
| LIST_REMOVE_I32 | 0xC7 |
| LIST_CLEAR | 0xC8 |
| NEW_LIST_F32 | 0xC9 |
| LIST_GET_F32 | 0xCA |
| LIST_SET_F32 | 0xCB |
| LIST_PUSH_F32 | 0xCC |
| LIST_POP_F32 | 0xCD |
| LIST_INSERT_F32 | 0xCE |
| LIST_REMOVE_F32 | 0xCF |
| STRING_LEN | 0xD0 |
| STRING_CONCAT | 0xD1 |
| STRING_GET_CHAR | 0xD2 |
| STRING_SLICE | 0xD3 |
| CALL_CHECK | 0xE0 |
| ADD_U32 | 0xE1 |
| SUB_U32 | 0xE2 |
| MUL_U32 | 0xE3 |
| DIV_U32 | 0xE4 |
| MOD_U32 | 0xE5 |
| ADD_U64 | 0xE6 |
| SUB_U64 | 0xE7 |
| MUL_U64 | 0xE8 |
| DIV_U64 | 0xE9 |
| MOD_U64 | 0xEA |
| CMP_EQ_U32 | 0xEB |
| CMP_NE_U32 | 0xEC |
| CMP_LT_U32 | 0xED |
| CMP_LE_U32 | 0xEE |
| CMP_GT_U32 | 0xEF |
| CMP_GE_U32 | 0xF0 |
| CMP_EQ_U64 | 0xF1 |
| CMP_NE_U64 | 0xF2 |
| CMP_LT_U64 | 0xF3 |
| CMP_LE_U64 | 0xF4 |
| CMP_GT_U64 | 0xF5 |
| CMP_GE_U64 | 0xF6 |
| AND_I64 | 0xD4 |
| OR_I64 | 0xD5 |
| XOR_I64 | 0xD6 |
| SHL_I64 | 0xD7 |
| SHR_I64 | 0xD8 |
| NEW_LIST_I64 | 0xD9 |
| LIST_GET_I64 | 0xDA |
| LIST_SET_I64 | 0xDB |
| LIST_PUSH_I64 | 0xDC |
| LIST_POP_I64 | 0xDD |
| LIST_INSERT_I64 | 0xDE |
| LIST_REMOVE_I64 | 0xDF |
| AND_I32 | 0xF7 |
| OR_I32 | 0xF8 |
| XOR_I32 | 0xF9 |
| SHL_I32 | 0xFA |
| SHR_I32 | 0xFB |

---



## 2. Control

- `NOP`
- `HALT`
- `TRAP`
- `BREAKPOINT`
- `JMP i32`
- `JMP_TRUE i32`
- `JMP_FALSE i32`
- `JMP_TABLE idx, i32` (jump table from CONST_POOL kind 6 + default offset)

---

## 3. Stack / Constants

- `POP`
- `DUP`
- `DUP2`
- `SWAP`
- `ROT`
- `CONST_I8 u8`
- `CONST_I16 u16`
- `CONST_I32 u32`
- `CONST_I64 u64`
- `CONST_I128 idx`
- `CONST_U8 u8`
- `CONST_U16 u16`
- `CONST_U32 u32`
- `CONST_U64 u64`
- `CONST_U128 idx`
- `CONST_F32 u32`
- `CONST_F64 u64`
- `CONST_BOOL u8`
- `CONST_CHAR u16`
- `CONST_STRING idx`
- `CONST_NULL`

---

## 4. Locals / Globals / Upvalues

- `LOAD_LOCAL idx`
- `STORE_LOCAL idx`
- `LOAD_GLOBAL idx`
- `STORE_GLOBAL idx`
- `LOAD_UPVALUE idx`
- `STORE_UPVALUE idx`

---

## 5. Arithmetic / Bitwise (Typed)

Type suffix `<T>` is one of:
`I8, I16, I32, I64, I128, U8, U16, U32, U64, U128, F32, F64`.

- `ADD_<T>`
- `SUB_<T>`
- `MUL_<T>`
- `DIV_<T>`
- `MOD_<T>` (integers only)
- `NEG_<T>`
- `INC_<T>` (optional)
- `DEC_<T>` (optional)
- `AND_<T>` (integers only)
- `OR_<T>`
- `XOR_<T>`
- `SHL_<T>`
- `SHR_<T>`

---

## 6. Comparisons (Typed)

All comparisons pop two values and push `bool`.

- `CMP_EQ_<T>`
- `CMP_NE_<T>`
- `CMP_LT_<T>`
- `CMP_LE_<T>`
- `CMP_GT_<T>`
- `CMP_GE_<T>`

---

## 7. Boolean

- `BOOL_NOT`
- `BOOL_AND`
- `BOOL_OR`

---

## 8. Conversions / Casts

- `TRUNC_<FROM>_<TO>`
- `SEXT_<FROM>_<TO>`
- `ZEXT_<FROM>_<TO>`
- `ITOF_<FROM>_<TO>`
- `FTOI_<FROM>_<TO>`
- `FPEXT_<FROM>_<TO>`
- `FPTRUNC_<FROM>_<TO>`
- `BITCAST_<FROM>_<TO>`

---

## 9. Memory / Objects

- `NEW_OBJECT idx`
- `NEW_CLOSURE idx, u8`
- `LOAD_FIELD idx`
- `STORE_FIELD idx`
- `IS_NULL`
- `REF_EQ`
- `REF_NE`
- `TYPE_OF`

---

## 10. Arrays / Lists / Strings

- `NEW_ARRAY idx, u32`
- `NEW_ARRAY_I64 idx, u32`
- `NEW_ARRAY_F32 idx, u32`
- `NEW_ARRAY_F64 idx, u32`
- `NEW_ARRAY_REF idx, u32`
- `ARRAY_LEN`
- `ARRAY_GET_<T>`
- `ARRAY_SET_<T>`
- `NEW_LIST idx, u32`
- `NEW_LIST_I64 idx, u32`
- `NEW_LIST_F32 idx, u32`
- `NEW_LIST_F64 idx, u32`
- `NEW_LIST_REF idx, u32`
- `LIST_LEN`
- `LIST_GET_<T>`
- `LIST_SET_<T>`
- `LIST_PUSH_<T>`
- `LIST_POP_<T>`
- `LIST_INSERT_<T>`
- `LIST_REMOVE_<T>`
- `LIST_CLEAR`
- `STRING_LEN`
- `STRING_GET_CHAR`
- `STRING_SLICE`
- `STRING_CONCAT`

`<T>` supports: `I32`, `I64`, `F32`, `F64`, `REF`.

---

## 11. Calls / Frames

- `CALL idx, u8`
- `CALL_INDIRECT idx, u8`
- `TAIL_CALL idx, u8`
- `RET`
- `ENTER u16`
- `LEAVE`

Note: In v0.1, imports are mapped into the Functions table at load time. `CALL` can target imports by function id; the runtime traps if the host has not resolved the import.

---

## 12. Debug / Profiling

- `LINE u32, u32`
- `PROFILE_START u32`
- `PROFILE_END u32`

---

## 13. Intrinsics / Native

- `INTRINSIC idx`
- `SYS_CALL idx`

---

## 14. Frozen Semantics (Operand Widths, Stack Effects, Traps)

All opcodes below are fixed for v0.1. The verifier and loader must enforce:
- Operand widths match the definitions here.
- Stack effects match the definitions here.
- Trap conditions must be raised where specified.

### 14.1 Control / Stack / Constants

| Opcode | Operands | Pops | Pushes | Trap Conditions |
|--------|----------|------|--------|-----------------|
| NOP | — | 0 | 0 | — |
| HALT | — | 0 | 0 | — |
| TRAP | — | 0 | 0 | always (runtime error) |
| BREAKPOINT | — | 0 | 0 | debug break if enabled |
| JMP | i32 | 0 | 0 | out-of-bounds target |
| JMP_TRUE | i32 | 1 | 0 | non-bool condition; out-of-bounds target |
| JMP_FALSE | i32 | 1 | 0 | non-bool condition; out-of-bounds target |
| JMP_TABLE | idx, i32 | 1 | 0 | non-i32 key; bad const blob; out-of-bounds target |
| POP | — | 1 | 0 | stack underflow |
| DUP | — | 1 | 2 | stack underflow |
| DUP2 | — | 2 | 4 | stack underflow |
| SWAP | — | 2 | 2 | stack underflow |
| ROT | — | 3 | 3 | stack underflow |
| CONST_I8 | u8 | 0 | 1 | — |
| CONST_I16 | u16 | 0 | 1 | — |
| CONST_I32 | u32 | 0 | 1 | — |
| CONST_I64 | u64 | 0 | 1 | — |
| CONST_I128 | idx | 0 | 1 | bad const id / blob |
| CONST_U8 | u8 | 0 | 1 | — |
| CONST_U16 | u16 | 0 | 1 | — |
| CONST_U32 | u32 | 0 | 1 | — |
| CONST_U64 | u64 | 0 | 1 | — |
| CONST_U128 | idx | 0 | 1 | bad const id / blob |
| CONST_F32 | u32 | 0 | 1 | — |
| CONST_F64 | u64 | 0 | 1 | — |
| CONST_BOOL | u8 | 0 | 1 | — |
| CONST_CHAR | u16 | 0 | 1 | — |
| CONST_STRING | idx | 0 | 1 | bad const id / string |
| CONST_NULL | — | 0 | 1 | — |

### 14.2 Locals / Globals / Upvalues

| Opcode | Operands | Pops | Pushes | Trap Conditions |
|--------|----------|------|--------|-----------------|
| LOAD_LOCAL | idx | 0 | 1 | local out of range |
| STORE_LOCAL | idx | 1 | 0 | local out of range |
| LOAD_GLOBAL | idx | 0 | 1 | global out of range |
| STORE_GLOBAL | idx | 1 | 0 | global out of range |
| LOAD_UPVALUE | idx | 0 | 1 | non-closure or upvalue out of range |
| STORE_UPVALUE | idx | 1 | 0 | non-closure or upvalue out of range |

### 14.3 Arithmetic / Bitwise / Compare / Bool

All opcodes in this section have **no operands** and use fixed stacks:

- Binary numeric/compare ops: Pops 2, pushes 1.
- Unary numeric ops (NEG/INC/DEC): Pops 1, pushes 1.
- BOOL_NOT: Pops 1, pushes 1.
- BOOL_AND/BOOL_OR: Pops 2, pushes 1.

Trap conditions:
- Division/mod by zero.
- Bad operand types (verification failure).
- Shift counts masked to bit width (no trap).

#### Integer Arithmetic (binary)

- I32: `ADD_I32`, `SUB_I32`, `MUL_I32`, `DIV_I32`, `MOD_I32`
- I64: `ADD_I64`, `SUB_I64`, `MUL_I64`, `DIV_I64`, `MOD_I64`
- U32: `ADD_U32`, `SUB_U32`, `MUL_U32`, `DIV_U32`, `MOD_U32`
- U64: `ADD_U64`, `SUB_U64`, `MUL_U64`, `DIV_U64`, `MOD_U64`

#### Float Arithmetic (binary)

- F32: `ADD_F32`, `SUB_F32`, `MUL_F32`, `DIV_F32`
- F64: `ADD_F64`, `SUB_F64`, `MUL_F64`, `DIV_F64`

#### Unary Numeric Ops

- Negation: `NEG_I8`, `NEG_I16`, `NEG_I32`, `NEG_I64`, `NEG_U8`, `NEG_U16`, `NEG_U32`, `NEG_U64`, `NEG_F32`, `NEG_F64`
- Increment: `INC_I8`, `INC_I16`, `INC_I32`, `INC_I64`, `INC_U8`, `INC_U16`, `INC_U32`, `INC_U64`, `INC_F32`, `INC_F64`
- Decrement: `DEC_I8`, `DEC_I16`, `DEC_I32`, `DEC_I64`, `DEC_U8`, `DEC_U16`, `DEC_U32`, `DEC_U64`, `DEC_F32`, `DEC_F64`

#### Comparisons (binary, push bool)

- I32: `CMP_EQ_I32`, `CMP_NE_I32`, `CMP_LT_I32`, `CMP_LE_I32`, `CMP_GT_I32`, `CMP_GE_I32`
- I64: `CMP_EQ_I64`, `CMP_NE_I64`, `CMP_LT_I64`, `CMP_LE_I64`, `CMP_GT_I64`, `CMP_GE_I64`
- U32: `CMP_EQ_U32`, `CMP_NE_U32`, `CMP_LT_U32`, `CMP_LE_U32`, `CMP_GT_U32`, `CMP_GE_U32`
- U64: `CMP_EQ_U64`, `CMP_NE_U64`, `CMP_LT_U64`, `CMP_LE_U64`, `CMP_GT_U64`, `CMP_GE_U64`
- F32: `CMP_EQ_F32`, `CMP_NE_F32`, `CMP_LT_F32`, `CMP_LE_F32`, `CMP_GT_F32`, `CMP_GE_F32`
- F64: `CMP_EQ_F64`, `CMP_NE_F64`, `CMP_LT_F64`, `CMP_LE_F64`, `CMP_GT_F64`, `CMP_GE_F64`

#### Bitwise / Shifts (binary)

- I32: `AND_I32`, `OR_I32`, `XOR_I32`, `SHL_I32`, `SHR_I32`
- I64: `AND_I64`, `OR_I64`, `XOR_I64`, `SHL_I64`, `SHR_I64`

#### Boolean

- `BOOL_NOT`, `BOOL_AND`, `BOOL_OR`

### 14.4 Calls / Frames / Conversions

| Opcode | Operands | Pops | Pushes | Trap Conditions |
|--------|----------|------|--------|-----------------|
| CALL | idx, u8 | dynamic | dynamic | bad method/sig; arg mismatch |
| CALL_INDIRECT | idx, u8 | dynamic | dynamic | bad target; arg mismatch |
| TAIL_CALL | idx, u8 | dynamic | dynamic | bad target; arg mismatch |
| RET | — | dynamic | 0 | return type mismatch |
| ENTER | u16 | 0 | 0 | locals mismatch |
| LEAVE | — | 0 | 0 | — |
| CONV_* | — | 1 | 1 | bad operand type (verify) |

### 14.5 Objects / Arrays / Lists / Strings

Common traps:
- Null refs for array/list/string ops.
- Index out of range (negative or >= len).
- Bad field/type IDs (verify/load).

Notes:
- `NEW_ARRAY` / `NEW_LIST` allocate 4-byte element containers (i32/u32/bool/char).
- Typed variants (`*_I64`, `*_F32`, `*_F64`, `*_REF`) fix element width to match the suffix.

| Opcode | Operands | Pops | Pushes |
|--------|----------|------|--------|
| NEW_OBJECT | idx | 0 | 1 |
| NEW_CLOSURE | idx, u8 | 0 | 1 |
| LOAD_FIELD | idx | 1 | 1 |
| STORE_FIELD | idx | 2 | 0 |
| IS_NULL | — | 1 | 1 |
| REF_EQ | — | 2 | 1 |
| REF_NE | — | 2 | 1 |
| TYPE_OF | — | 1 | 1 |
| NEW_ARRAY | idx, u32 | 0 | 1 |
| NEW_ARRAY_I64 | idx, u32 | 0 | 1 |
| NEW_ARRAY_F32 | idx, u32 | 0 | 1 |
| NEW_ARRAY_F64 | idx, u32 | 0 | 1 |
| NEW_ARRAY_REF | idx, u32 | 0 | 1 |
| ARRAY_LEN | — | 1 | 1 |
| ARRAY_GET_I32 | — | 2 | 1 |
| ARRAY_GET_I64 | — | 2 | 1 |
| ARRAY_GET_F32 | — | 2 | 1 |
| ARRAY_GET_F64 | — | 2 | 1 |
| ARRAY_GET_REF | — | 2 | 1 |
| ARRAY_SET_I32 | — | 3 | 0 |
| ARRAY_SET_I64 | — | 3 | 0 |
| ARRAY_SET_F32 | — | 3 | 0 |
| ARRAY_SET_F64 | — | 3 | 0 |
| ARRAY_SET_REF | — | 3 | 0 |
| NEW_LIST | idx, u32 | 0 | 1 |
| NEW_LIST_I64 | idx, u32 | 0 | 1 |
| NEW_LIST_F32 | idx, u32 | 0 | 1 |
| NEW_LIST_F64 | idx, u32 | 0 | 1 |
| NEW_LIST_REF | idx, u32 | 0 | 1 |
| LIST_LEN | — | 1 | 1 |
| LIST_GET_I32 | — | 2 | 1 |
| LIST_GET_I64 | — | 2 | 1 |
| LIST_GET_F32 | — | 2 | 1 |
| LIST_GET_F64 | — | 2 | 1 |
| LIST_GET_REF | — | 2 | 1 |
| LIST_SET_I32 | — | 3 | 0 |
| LIST_SET_I64 | — | 3 | 0 |
| LIST_SET_F32 | — | 3 | 0 |
| LIST_SET_F64 | — | 3 | 0 |
| LIST_SET_REF | — | 3 | 0 |
| LIST_PUSH_I32 | — | 2 | 0 |
| LIST_PUSH_I64 | — | 2 | 0 |
| LIST_PUSH_F32 | — | 2 | 0 |
| LIST_PUSH_F64 | — | 2 | 0 |
| LIST_PUSH_REF | — | 2 | 0 |
| LIST_POP_I32 | — | 1 | 1 |
| LIST_POP_I64 | — | 1 | 1 |
| LIST_POP_F32 | — | 1 | 1 |
| LIST_POP_F64 | — | 1 | 1 |
| LIST_POP_REF | — | 1 | 1 |
| LIST_INSERT_I32 | — | 3 | 0 |
| LIST_INSERT_I64 | — | 3 | 0 |
| LIST_INSERT_F32 | — | 3 | 0 |
| LIST_INSERT_F64 | — | 3 | 0 |
| LIST_INSERT_REF | — | 3 | 0 |
| LIST_REMOVE_I32 | — | 2 | 1 |
| LIST_REMOVE_I64 | — | 2 | 1 |
| LIST_REMOVE_F32 | — | 2 | 1 |
| LIST_REMOVE_F64 | — | 2 | 1 |
| LIST_REMOVE_REF | — | 2 | 1 |
| LIST_CLEAR | — | 1 | 0 |
| STRING_LEN | — | 1 | 1 |
| STRING_GET_CHAR | — | 2 | 1 |
| STRING_SLICE | — | 3 | 1 |
| STRING_CONCAT | — | 2 | 1 |

### 14.6 Debug / Profiling / Intrinsics

| Opcode | Operands | Pops | Pushes | Trap Conditions |
|--------|----------|------|--------|-----------------|
| LINE | u32, u32 | 0 | 0 | — |
| PROFILE_START | u32 | 0 | 0 | — |
| PROFILE_END | u32 | 0 | 0 | — |
| INTRINSIC | idx | dynamic | dynamic | bad id/signature |
| SYS_CALL | idx | dynamic | dynamic | bad id/signature |


---

### Rules

# SBC Rules (Design Requirements)

This document defines loader and verifier rules for SBC modules.

---

## 1. Loader Rules

### File / Header
- Header must be valid per `SBC_Headers.md`.
- Version must be supported; otherwise reject.
- Endianness must be little-endian in v0.1.

### Sections
- Section table must be within file bounds.
- Section IDs must be unique and known.
- Section ranges must not overlap.
- All table row counts must match section sizes exactly.

### Heaps
- String heap offsets must point to null-terminated UTF-8 strings.
- Blob offsets must point to length-prefixed blobs within bounds.

---

## 2. Index Validation

- All `idx` operands must be `< row_count` of their referenced table.
- `field_start + field_count` must be within Field table bounds.
- `method_id` must reference a valid Method row.
- `code_offset + code_size` must be within CODE section.

---

## 3. Structural Bytecode Rules

- Jump targets must land on instruction boundaries.
- Instruction operands must be within function bounds.
- `ENTER` local count must match method row `local_count`.
- `CALL`/`TAILCALL` arg count must match signature param count.

---

## 4. Verifier Rules (IL-Style)

The verifier ensures stack/type safety before execution or JIT.

- Stack height never goes negative.
- Stack height must match at all control-flow joins.
- Stack value types must match at joins.
- Locals must be assigned before use.
- Each opcode must receive operand types it expects (e.g., `ADD_I32` pops two `i32`).
- Return value type must match method signature.

---

## 5. Trust Policy

- Verified modules may skip verification if `flags.verified` is set and policy allows.
- Unverified modules must be verified in safe mode.
- Verification failures are fatal in safe mode.


---

### VM Opcode Spec (Legacy)

# Simple VM Opcode Specification
## Version 0.1 (draft)

This document defines the Simple VM bytecode instruction set. It is intended to be implemented in a portable C++ runtime and consumed by the Simple compiler.

---

## Goals

- Portable, deterministic bytecode
- Fast interpreter dispatch in C++
- Clear, typed operations (minimal runtime type checks)
- Stable, versioned encoding

---

## VM Model (High Level)

- **Typed stack VM** with explicit locals and globals.
- Each function has:
  - A local slot array (fixed size).
  - An evaluation stack (value stack).
- A call frame stores: return address, base pointer, locals, and stack depth.
- Heap objects: strings, arrays, lists, artifacts, and closures.

---

## Bytecode Encoding

- Endianness: **little-endian**.
- Instruction format: **opcode (u8) + operands**.
- Operand sizes are fixed per opcode (u8/u16/u32/u64/i32).
- Jump targets use **signed i32 relative byte offsets** from the next instruction.
- Constants are stored in a constant pool; `CONST_*` may use immediate operands or a pool index.

### Operand Types

- `u8`, `u16`, `u32`, `u64`
- `i32` (relative offset)
- `idx` (u32 index into tables: constants, globals, locals, types, fields, functions)

---

## Metadata Tables (C#-style)

Bytecode modules carry structured metadata tables so the VM and JIT can resolve types, fields, and methods without guessing.

- **Type Table**: name, kind (primitive/struct/artifact/module/enum), size, flags, field range
- **Field Table**: name, type id, offset, flags (mutable/immutable)
- **Method Table**: name, signature id, code offset, local count, flags (static/instance)
- **Signature Table**: parameter types, return type, calling convention
- **Constant Pool**: strings, numeric blobs, type refs
- **Debug Table** (optional): line mappings, source file refs

All `idx` operands reference these tables.

---

## Module Binary Layout (v0.1)

All multi-byte fields are **little-endian**.

### File Header (fixed 32 bytes)

```
u32 magic        // 'S','B','C','0' = 0x30434253
u16 version      // 0x0001
u8  endian       // 1 = little
u8  flags        // bit0=has_debug, bit1=verified, bit2=jit_hint
u32 section_count
u32 section_table_offset
u32 entry_method_id
u32 reserved0
u32 reserved1
```

### Section Table (variable)

Each section entry is 16 bytes:

```
u32 id       // section kind
u32 offset   // file offset
u32 size     // bytes
u32 count    // row count (0 if not table)
```

Section IDs:
```
1 = TYPES
2 = FIELDS
3 = METHODS
4 = SIGS
5 = CONST_POOL
6 = GLOBALS
7 = FUNCTIONS
8 = CODE
9 = DEBUG
```

### Heaps and Pools

String heap is UTF-8, null-terminated, with a 0th empty string at offset 0.
Blob heap is raw bytes; entries are length-prefixed with `u32`.

---

## Table Row Formats (v0.1)

All indices are `u32` unless noted.

### Type Row
```
u32 name_str
u8  kind          // 0=primitive,1=struct,2=artifact,3=module,4=enum
u8  flags         // bit0=ref_type, bit1=generic, bit2=sealed
u16 reserved
u32 size          // size in bytes for value types, 0 for ref types
u32 field_start   // index into field table
u32 field_count
```

### Field Row
```
u32 name_str
u32 type_id
u32 offset        // byte offset within instance
u32 flags         // bit0=mutable, bit1=static
```

### Method Row
```
u32 name_str
u32 sig_id
u32 code_offset   // offset into CODE section
u16 local_count
u16 flags         // bit0=static, bit1=instance, bit2=virtual
```

### Signature Row
```
u32 ret_type_id
u16 param_count
u16 call_conv     // 0=default, 1=varargs
u32 param_type_start // index into a packed param list (see below)
```

### Param Type List (packed)
`param_type_start` points into a packed `u32` array of type ids.

### Constant Pool Entry
Each entry is `u32 kind` + payload:
```
0 = STRING  : u32 str_offset
1 = I128    : u32 blob_offset
2 = U128    : u32 blob_offset
3 = F32     : u32 bits
4 = F64     : u64 bits
5 = TYPE    : u32 type_id
```

### Globals Table
```
u32 name_str
u32 type_id
u32 flags         // bit0=mutable
u32 init_const_id // const pool id, or 0xFFFFFFFF for zero-init
```

### Functions Table
```
u32 method_id
u32 code_offset
u32 code_size
u32 stack_max
```

---

## Validation Rules (Loader + Verifier)

These rules are checked by the loader before execution and by the verifier when enabled.

### File/Section Rules

- Header magic must match `0x30434253`.
- Version must be supported; otherwise reject.
- Section table offset/size must be within file bounds.
- Section IDs must be unique and known (unknown IDs rejected in v0.1).
- All section ranges must be non-overlapping and within file bounds.
- `CODE` section must be present if any function exists.
- `DEBUG` section may be absent even if `flags.has_debug` is set (treated as warning).

### Table Rules

- Row counts must match section sizes exactly.
- Any index (`idx`) must be `< row_count` of its referenced table.
- String heap offsets must point to a null-terminated string within heap bounds.
- Blob offsets must point to a length-prefixed blob within heap bounds.

### Type/Field/Method Rules

- `field_start + field_count` must be within the field table.
- `method_id` must reference a valid method row.
- `code_offset + code_size` must be within the CODE section.
- `stack_max` must be >= actual stack usage (verified after bytecode validation).

### Bytecode Rules (Structural)

- All instruction operands are within function code bounds.
- Jump targets land on valid instruction boundaries.
- `ENTER` local count must match the method row `local_count`.
- `CALL`/`TAILCALL` arg count must match signature param count.

### Verification Rules (Type/Stack Safety)

- Stack height never goes negative and matches at merge points.
- Stack value types are consistent across control-flow joins.
- Locals are assigned before use.
- All opcodes receive operand types they expect (e.g., `ADD_I32` pops `i32, i32`).

---

## Debug Section Schema (v0.1)

The DEBUG section provides source mapping and optional symbol names. All offsets are relative to the start of the DEBUG section.

### Debug Header

```
u32 file_count
u32 line_count
u32 sym_count
u32 reserved
```

### File Table (file_count rows)

```
u32 file_name_str   // string heap offset (UTF-8)
u32 file_hash       // optional hash (0 if unused)
```

### Line Table (line_count rows)

Maps bytecode offsets to source locations.

```
u32 method_id       // method row index
u32 code_offset     // offset within CODE section
u32 file_id         // index into file table
u32 line            // 1-based
u32 column          // 1-based
```

### Symbol Table (sym_count rows)

Optional names for tools (e.g., globals, locals, params). Tools should ignore unknown kinds.

```
u32 kind            // 0=global,1=local,2=param,3=type,4=field,5=method
u32 owner_id        // e.g., method_id for local/param, type_id for field
u32 symbol_id       // e.g., global_id, local_index, param_index
u32 name_str
```

### Debug Validation Rules

- `file_id` and `method_id` must be valid indices.
- `code_offset` must be within the method's code range.
- `line` and `column` must be >= 1.

## Verification (IL-Style)

Bytecode can be verified before execution or JIT to ensure type and stack safety.

Required checks:
- Stack height never goes negative and matches at merge points.
- Stack value types are consistent across control-flow joins.
- Locals are assigned before use.
- Branch targets align to instruction boundaries and valid blocks.
- Call sites match method signatures (arg count and types).
- No invalid field/method/type indices.

Verification is required for "safe" execution modes and optional for trusted code.

---

## JIT Strategy (Tiered)

- **Tier 0 (Quick JIT)**: fast compile, minimal optimization, prioritizes startup.
- **Tier 1 (Optimizing JIT)**: recompiles hot methods using profiling counters.
- **Interpreter Fallback**: used when JIT is disabled or compilation fails.

Hotness is tracked per-method (call counts or time-based counters). Tiering policy is VM-configurable.

---

## Memory Management

The VM uses a **tracing GC**:
- Root set: globals, call stacks, locals, operand stack, and pinned handles.
- Collection strategy: mark-sweep or generational (implementation detail).

---

## Opcode Groups

1. Control
2. Stack / Constants
3. Locals / Globals / Upvalues
4. Arithmetic / Bitwise
5. Comparisons
6. Boolean
7. Conversions / Casts
8. Memory / Objects
9. Arrays / Lists / Strings
10. Calls / Frames
11. Debug / Profiling
12. Intrinsics / Native

---

## Opcode List (By Group)

### 1) Control

- `NOP`
- `HALT`
- `TRAP` (runtime error)
- `BREAKPOINT`
- `JMP i32`
- `JMP_TRUE i32` (pops bool)
- `JMP_FALSE i32` (pops bool)
- `JMP_TABLE idx, i32` (jump table + default)

### 2) Stack / Constants

- `POP`
- `DUP`
- `DUP2`
- `SWAP`
- `ROT`
- `CONST_I8 u8`
- `CONST_I16 u16`
- `CONST_I32 u32`
- `CONST_I64 u64`
- `CONST_I128 idx` (const pool)
- `CONST_U8 u8`
- `CONST_U16 u16`
- `CONST_U32 u32`
- `CONST_U64 u64`
- `CONST_U128 idx` (const pool)
- `CONST_F32 u32` (bit pattern)
- `CONST_F64 u64` (bit pattern)
- `CONST_BOOL u8`
- `CONST_CHAR u16`
- `CONST_STRING idx` (const pool)
- `CONST_NULL`

### 3) Locals / Globals / Upvalues

- `LOAD_LOCAL idx`
- `STORE_LOCAL idx`
- `LOAD_GLOBAL idx`
- `STORE_GLOBAL idx`
- `LOAD_UPVALUE idx`
- `STORE_UPVALUE idx`

### 4) Arithmetic / Bitwise

The following instructions are **typed**. `<T>` is one of:
`I8, I16, I32, I64, I128, U8, U16, U32, U64, U128, F32, F64`.

- `ADD_<T>`
- `SUB_<T>`
- `MUL_<T>`
- `DIV_<T>`
- `MOD_<T>` (integers only)
- `NEG_<T>`
- `INC_<T>` (optional)
- `DEC_<T>` (optional)
- `AND_<T>` (integers only)
- `OR_<T>`  (integers only)
- `XOR_<T>` (integers only)
- `SHL_<T>` (integers only)
- `SHR_<T>` (integers only)

### 5) Comparisons

All comparisons pop two values and push `bool`.

- `CMP_EQ_<T>`
- `CMP_NE_<T>`
- `CMP_LT_<T>`
- `CMP_LE_<T>`
- `CMP_GT_<T>`
- `CMP_GE_<T>`

### 6) Boolean

- `BOOL_NOT`
- `BOOL_AND`
- `BOOL_OR`

### 7) Conversions / Casts

Typed conversion patterns:

- `TRUNC_<FROM>_<TO>`
- `SEXT_<FROM>_<TO>`
- `ZEXT_<FROM>_<TO>`
- `ITOF_<FROM>_<TO>`
- `FTOI_<FROM>_<TO>`
- `FPEXT_<FROM>_<TO>`
- `FPTRUNC_<FROM>_<TO>`
- `BITCAST_<FROM>_<TO>`

### 8) Memory / Objects

- `NEW_OBJECT idx` (type id)
- `NEW_CLOSURE idx, u8` (function id, upvalue count)
- `LOAD_FIELD idx` (field id)
- `STORE_FIELD idx` (field id)
- `IS_NULL`
- `REF_EQ`
- `REF_NE`
- `TYPEOF`

### 9) Arrays / Lists / Strings

- `NEW_ARRAY idx, u32` (element type id, length)
- `ARRAY_LEN`
- `ARRAY_GET_<T>`
- `ARRAY_SET_<T>`
- `NEW_LIST idx, u32` (element type id, capacity)
- `LIST_LEN`
- `LIST_GET_<T>`
- `LIST_SET_<T>`
- `LIST_PUSH_<T>`
- `LIST_POP_<T>`
- `LIST_INSERT_<T>`
- `LIST_REMOVE_<T>`
- `LIST_CLEAR`
- `STRING_LEN`
- `STRING_GET_CHAR`
- `STRING_SLICE`
- `STRING_CONCAT`

### 10) Calls / Frames

- `CALL idx, u8` (function id, arg count)
- `CALL_INDIRECT idx, u8` (signature id, arg count)
- `TAILCALL idx, u8`
- `RET`
- `ENTER u16` (local count)
- `LEAVE`

### 11) Debug / Profiling

- `LINE u32, u32` (line, column)
- `PROFILE_START u32`
- `PROFILE_END u32`

### 12) Intrinsics / Native

- `INTRINSIC idx` (built-in id: print, len, etc)
- `SYS_CALL idx` (optional host interop)

---

## Reserved / Future

- Exception handling (`TRY`, `CATCH`, `THROW`)
- Concurrency primitives (`SPAWN`, `JOIN`)
- SIMD instructions
- AOT packaging and native image formats

---

## Notes

- This list is intentionally **extensive** but versioned; opcodes may be added in new VM versions.
- Numeric suffixes are **explicit** to avoid runtime type checks and improve interpreter speed.
- The compiler is expected to emit the most specific opcode available for a given type.


---
