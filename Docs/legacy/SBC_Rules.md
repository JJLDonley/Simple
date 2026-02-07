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
