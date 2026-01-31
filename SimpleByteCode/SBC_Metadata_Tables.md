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
