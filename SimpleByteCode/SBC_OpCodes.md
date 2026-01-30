# SBC OpCodes (Design Requirements)

This document defines the opcode set for Simple VM bytecode (v0.1).

---

## 1. Opcode Encoding

- Each opcode is a single **u8**.
- Operands are fixed-size and defined per opcode.
- Typed opcodes use explicit suffixes (e.g., `ADD_I32`).

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
