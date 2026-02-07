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
