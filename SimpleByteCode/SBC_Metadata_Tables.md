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

```
struct TypeRow {
  u32 name_str;
  u8  kind;        // 0=primitive,1=struct,2=artifact,3=module,4=enum
  u8  flags;       // bit0=ref_type, bit1=generic, bit2=sealed
  u16 reserved;
  u32 size;        // bytes for value types, 0 for ref types
  u32 field_start; // index into Field table
  u32 field_count;
}
```

Requirements:
- `field_start + field_count` must be within the Field table.

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
