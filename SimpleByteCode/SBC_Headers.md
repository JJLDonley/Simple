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
