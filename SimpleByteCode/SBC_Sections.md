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
