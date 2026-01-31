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
- `TYPEOF`

---

## 10. Arrays / Lists / Strings

- `NEW_ARRAY idx, u32`
- `ARRAY_LEN`
- `ARRAY_GET_<T>`
- `ARRAY_SET_<T>`
- `NEW_LIST idx, u32`
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
- `TAILCALL idx, u8`
- `RET`
- `ENTER u16`
- `LEAVE`

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

All typed arithmetic and compare ops have no operands and use fixed stacks:

- Binary numeric/compare ops: Pops 2, pushes 1.
- Unary numeric ops (NEG/INC/DEC): Pops 1, pushes 1.
- BOOL_NOT: Pops 1, pushes 1.
- BOOL_AND/BOOL_OR: Pops 2, pushes 1.

Trap conditions:
- Division/mod by zero.
- Bad operand types (verification failure).
- Shift counts masked to bit width (no trap).

### 14.4 Calls / Frames / Conversions

| Opcode | Operands | Pops | Pushes | Trap Conditions |
|--------|----------|------|--------|-----------------|
| CALL | idx, u8 | dynamic | dynamic | bad method/sig; arg mismatch |
| CALL_INDIRECT | idx, u8 | dynamic | dynamic | bad target; arg mismatch |
| TAILCALL | idx, u8 | dynamic | dynamic | bad target; arg mismatch |
| RET | — | dynamic | 0 | return type mismatch |
| ENTER | u16 | 0 | 0 | locals mismatch |
| LEAVE | — | 0 | 0 | — |
| CONV_* | — | 1 | 1 | bad operand type (verify) |

### 14.5 Objects / Arrays / Lists / Strings

Common traps:
- Null refs for array/list/string ops.
- Index out of range (negative or >= len).
- Bad field/type IDs (verify/load).

| Opcode | Operands | Pops | Pushes |
|--------|----------|------|--------|
| NEW_OBJECT | idx | 0 | 1 |
| NEW_CLOSURE | idx, u8 | 0 | 1 |
| LOAD_FIELD | idx | 1 | 1 |
| STORE_FIELD | idx | 2 | 0 |
| IS_NULL | — | 1 | 1 |
| REF_EQ / REF_NE | — | 2 | 1 |
| TYPEOF | — | 1 | 1 |
| NEW_ARRAY | idx, u32 | 0 | 1 |
| ARRAY_LEN | — | 1 | 1 |
| ARRAY_GET_<T> | — | 2 | 1 |
| ARRAY_SET_<T> | — | 3 | 0 |
| NEW_LIST | idx, u32 | 0 | 1 |
| LIST_LEN | — | 1 | 1 |
| LIST_GET_<T> | — | 2 | 1 |
| LIST_SET_<T> | — | 3 | 0 |
| LIST_PUSH_<T> | — | 2 | 0 |
| LIST_POP_<T> | — | 1 | 1 |
| LIST_INSERT_<T> | — | 3 | 0 |
| LIST_REMOVE_<T> | — | 2 | 1 |
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
