# SBC Debug Section (Design Requirements)

This document defines the DEBUG section schema for SBC modules.

---

## 1. Debug Header

```
struct DebugHeader {
  u32 file_count;
  u32 line_count;
  u32 sym_count;
  u32 reserved;
}
```

- `reserved` must be `0` in v0.1.

---

## 2. File Table

```
struct DebugFileRow {
  u32 file_name_str; // string heap offset
  u32 file_hash;     // optional hash, 0 if unused
}
```

- `file_name_str` must be a valid string heap offset.

---

## 3. Line Table

```
struct DebugLineRow {
  u32 method_id;
  u32 code_offset; // offset within CODE section
  u32 file_id;     // index into file table
  u32 line;        // 1-based
  u32 column;      // 1-based
}
```

---

## 4. Symbol Table

```
struct DebugSymRow {
  u32 kind;      // 0=global,1=local,2=param,3=type,4=field,5=method
  u32 owner_id;  // method_id for locals/params, type_id for fields
  u32 symbol_id; // local index / param index / global id
  u32 name_str;  // string heap offset
}
```

---

## 5. Validation Rules

- `file_id` and `method_id` must be valid indices.
- `code_offset` must be within the method's code range.
- `line` and `column` must be >= 1.
