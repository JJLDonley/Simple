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
