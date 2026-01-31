# SBC ABI (Draft)

This document defines the stable ABI for SBC modules, intrinsics, and host FFI.

---

## 1. Intrinsic Calling Convention

- Intrinsic ID: `u32`, stable, global namespace.
- Signature: VM primitive types only (`i32/i64/f32/f64/ref/void`).
- Errors: Intrinsics trap with a diagnostic message; no exceptions.
- Side effects: Only as documented per intrinsic.

### VM Primitive Type Codes
- `0 = void`
- `1 = i32`
- `2 = i64`
- `3 = f32`
- `4 = f64`
- `5 = ref`

### Intrinsic ID Table (v0.1)
| ID | Name | Params | Return | Notes |
|----|------|--------|--------|-------|
| 0x0000 | core.debug.trap | i32 | void | Trap with code (message optional via DEBUG table). |
| 0x0001 | core.debug.breakpoint | void | void | No-op in release, debugger hook in dev. |
| 0x0010 | core.debug.log_i32 | i32 | void | Debug log; optional in release. |
| 0x0011 | core.debug.log_i64 | i64 | void | Debug log; optional in release. |
| 0x0012 | core.debug.log_f32 | f32 | void | Debug log; optional in release. |
| 0x0013 | core.debug.log_f64 | f64 | void | Debug log; optional in release. |
| 0x0014 | core.debug.log_ref | ref | void | Debug log; prints ref id. |
| 0x0020 | core.math.abs_i32 | i32 | i32 | Pure. |
| 0x0021 | core.math.abs_i64 | i64 | i64 | Pure. |
| 0x0022 | core.math.min_i32 | i32, i32 | i32 | Pure. |
| 0x0023 | core.math.max_i32 | i32, i32 | i32 | Pure. |
| 0x0024 | core.math.min_i64 | i64, i64 | i64 | Pure. |
| 0x0025 | core.math.max_i64 | i64, i64 | i64 | Pure. |
| 0x0026 | core.math.min_f32 | f32, f32 | f32 | Pure. |
| 0x0027 | core.math.max_f32 | f32, f32 | f32 | Pure. |
| 0x0028 | core.math.min_f64 | f64, f64 | f64 | Pure. |
| 0x0029 | core.math.max_f64 | f64, f64 | f64 | Pure. |
| 0x0030 | core.time.mono_ns | void | i64 | Monotonic time in ns. |
| 0x0031 | core.time.wall_ns | void | i64 | Wall clock time in ns. |
| 0x0040 | core.rand.u32 | void | i32 | PRNG/OS entropy; impl-defined. |
| 0x0041 | core.rand.u64 | void | i64 | PRNG/OS entropy; impl-defined. |
| 0x0050 | core.io.write_stdout | ref, i32 | void | Writes bytes from blob/string handle; length in i32. |
| 0x0051 | core.io.write_stderr | ref, i32 | void | Writes bytes from blob/string handle; length in i32. |

---

## 1.1 Core Library OS Services (FFI-backed)

These are *core library* contracts that are expected to be implemented by OS/host FFI.
They are not VM opcodes, and not VM intrinsics. They live in the import table.

### Module: `core.os`
| Symbol | Params | Return | Notes |
|--------|--------|--------|-------|
| args_count | void | i32 | Returns argc. |
| args_get | i32 | ref | Returns argv[i] as string ref. |
| env_get | ref | ref | Input: name string ref, returns value string ref or null. |
| cwd_get | void | ref | Returns current working directory. |
| time_mono_ns | void | i64 | Monotonic time in ns. |
| time_wall_ns | void | i64 | Wall clock time in ns. |
| sleep_ms | i32 | void | Best-effort sleep. |

### Module: `core.fs`
| Symbol | Params | Return | Notes |
|--------|--------|--------|-------|
| open | ref, i32 | i32 | Path, flags -> fd (or -1). |
| close | i32 | void | Close fd. |
| read | i32, ref, i32 | i32 | fd, buffer ref, len -> bytes read (or -1). |
| write | i32, ref, i32 | i32 | fd, buffer ref, len -> bytes written (or -1). |

### Module: `core.log`
| Symbol | Params | Return | Notes |
|--------|--------|--------|-------|
| log | ref, i32 | void | Writes bytes from buffer ref with length. |

---

## 2. FFI Tables (Import/Export)

### Import Table Layout (per entry)
- `module_name_str` (u32, string heap offset)
- `symbol_name_str` (u32, string heap offset)
- `sig_id` (u32, signature table index)
- `flags` (u32)

### Export Table Layout (per entry)
- `symbol_name_str` (u32, string heap offset)
- `func_id` (u32, functions table index)
- `flags` (u32)
- `reserved` (u32)

### FFI Flags
- `0x0001`: can_trap (host may return trap)
- `0x0002`: pure (no side effects)
- `0x0004`: no_gc (host will not allocate or trigger GC)
- `0x0008`: allow_ref (host accepts/returns ref handles)

### Calling Convention
- Args passed in VM order, stack top = last arg.
- Returns: 0 or 1 slot (per signature).
- Ref values are opaque handles; host must not assume pointer layout.
- ABI version must be declared in SBC header flags or a dedicated field.

---

## 3. Host API (C ABI, Draft)

```c
// Lifetime
void sbc_ref_retain(uint32_t handle);
void sbc_ref_release(uint32_t handle);

// Type discovery
uint32_t sbc_ref_type_id(uint32_t handle);
uint32_t sbc_ref_kind(uint32_t handle);

// String access (UTF-8)
uint32_t sbc_string_len_utf8(uint32_t handle);
uint32_t sbc_string_copy_utf8(uint32_t handle, char* out, uint32_t out_cap);

// Blob access (raw bytes)
uint32_t sbc_blob_len(uint32_t handle);
uint32_t sbc_blob_copy(uint32_t handle, uint8_t* out, uint32_t out_cap);

// Struct access (opaque, byte-level)
uint32_t sbc_struct_size(uint32_t handle);
bool sbc_struct_read(uint32_t handle, uint32_t offset, uint8_t* out, uint32_t size);
bool sbc_struct_write(uint32_t handle, uint32_t offset, const uint8_t* data, uint32_t size);

// Array access (typed)
uint32_t sbc_array_len(uint32_t handle);
bool sbc_array_get_i32(uint32_t handle, uint32_t index, int32_t* out);
bool sbc_array_get_i64(uint32_t handle, uint32_t index, int64_t* out);
bool sbc_array_get_f32(uint32_t handle, uint32_t index, float* out);
bool sbc_array_get_f64(uint32_t handle, uint32_t index, double* out);
bool sbc_array_get_ref(uint32_t handle, uint32_t index, uint32_t* out);

// List access (typed)
uint32_t sbc_list_len(uint32_t handle);
bool sbc_list_get_i32(uint32_t handle, uint32_t index, int32_t* out);
bool sbc_list_get_i64(uint32_t handle, uint32_t index, int64_t* out);
bool sbc_list_get_f32(uint32_t handle, uint32_t index, float* out);
bool sbc_list_get_f64(uint32_t handle, uint32_t index, double* out);
bool sbc_list_get_ref(uint32_t handle, uint32_t index, uint32_t* out);
```

### Host API Tiers
- Tier 0 (minimum read-only): ref retain/release, ref kind/type, struct size/read, string len/copy.
- Tier 1 (write-back): struct write, list/array len + get.
- Tier 2 (advanced): blob access + list/array set/push/pop (if exposed).
