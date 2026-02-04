# Simple::VM

**Scope**  
Runtime execution engine, heap/GC, JIT, and runtime diagnostics.

**How It Works**  
The VM executes verified SBC bytecode using an untagged slot runtime. Types are enforced by the
verifier and metadata; runtime slots are raw values. Heap objects (strings/arrays/lists/closures)
are managed by GC, and tiered JIT can optimize hot methods.

**What Works (Current)**  
- Interpreter, verifier integration, and untagged slot runtime.  
- Heap objects and GC safepoints with ref bitmaps/stack maps.  
- Tiered JIT path and runtime trap diagnostics.  
- ABI/FFI surface frozen for v0.1 (see `Docs/legacy/SBC_ABI.md`).  

**Implementation Plan**  
See `Simple/Docs/Implementation.md` (Module: Simple::VM).

---

## Specification (Legacy Reference, Organized)

### Runtime

# SBC Runtime (Design Requirements)

This document defines the runtime execution model for the Simple VM.

---

## 1. VM Model

- **Typed stack VM** with explicit locals and globals.
- Each function has:
  - A fixed local slot array.
  - An evaluation stack.
- A call frame stores:
  - Return address
  - Base pointer
  - Locals array
  - Stack depth at entry

---

## 2. Execution Modes

- **Interpreter**: baseline execution of bytecode.
- **Tiered JIT**:
  - Tier 0 quick compile (minimal optimization).
  - Tier 1 optimizing compile (hot methods).
- JIT compiled code must respect the same verification rules.

---

## 3. Verification

- IL-style verifier is required in safe mode.
- Unverified modules must pass verification before JIT or execution.

---

## 4. Memory Management

- **Tracing GC** (mark-sweep or generational implementation).
- Root set includes:
  - Globals
  - Call frames
  - Locals
  - Operand stacks
  - Pinned handles

---

## 5. Object Model

- Heap objects include strings, arrays, lists, artifacts, and closures.
- Each object has a header with:
  - `type_id`
  - `size`
  - GC metadata

---

## 6. Calling Conventions

- Arguments are pushed left-to-right.
- `CALL` pops args and transfers control.
- `RET` restores frame and pushes return value (if any).

---

## 7. Error Handling

- Runtime errors trigger `TRAP`.
- Uncaught `TRAP` terminates execution with diagnostic info.


---

### Debug

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


---

### ABI / FFI

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

## 1.2 SysCall IDs (v0.1)

SysCalls are **reserved** in v0.1 and must not appear in verified modules.

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

**Buffer layout (v0.1)**: `buffer ref` is a VM `Array` with element size 4 (I32). Each element stores one byte in the low 8 bits. The `len` argument is the number of bytes to read/write; the VM clamps to the array length.

### Module: `core.log`
| Symbol | Params | Return | Notes |
|--------|--------|--------|-------|
| log | ref, i32 | void | Writes bytes from buffer ref with length. |

---

## 1.2 FFI Struct Layout (v0.1)

FFI structs are **concrete and platform-ABI compatible** in v0.1.

Rules:
- Field order is **exactly as declared** (no reordering).
- Each field is aligned to its **natural alignment**: 1, 2, 4, 8, or 16 bytes.
- Struct alignment = max field alignment.
- Struct size is padded to a multiple of struct alignment.
- Endianness is **little-endian** for all scalar fields.
- Only VM value types (`i32/i64/f32/f64`) are allowed as struct fields in v0.1.
- `ref` is **not** a valid struct field for FFI unless explicitly documented for that API.
- No packed/bitfield structs in v0.1.

---

## 1.3 Core Library Namespaces (v0.1)

### Opcode-Backed (NOT imports)
- `core.array.*` (all Array* opcodes)
- `core.list.*` (all List* opcodes)
- `core.string.*` (StringLen/StringConcat/StringGetChar/StringSlice)
- `core.ref.*` (IsNull/RefEq/RefNe/TypeOf)
- `core.object.*` (NewObject/LoadField/StoreField/NewClosure/LoadUpvalue/StoreUpvalue)
- `core.num.*` (all numeric ops and conversions)
- `core.ctrl.*` (Jmp/JmpTrue/JmpFalse/JmpTable/Call/TailCall/Ret/Enter/Leave)

### Intrinsic-Backed
- `core.debug.*` (trap/breakpoint/log_*)
- `core.math.*` (abs/min/max)
- `core.time.*` (mono_ns/wall_ns)
- `core.rand.*` (u32/u64)
- `core.io.*` (write_stdout/write_stderr)

### FFI Import-Backed
- `core.os.*`
- `core.fs.*`
- `core.log.*`

Notes:
- Core library functions in `core.*` must not overlap in name between opcode, intrinsic, and import spaces.
- If a capability is opcode-backed, it must NOT appear as a core import in v0.1.

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

Validation (v0.1):
- String offsets must point to valid null-terminated UTF-8 in CONST_POOL.
- `sig_id` must reference a valid signature.
- `func_id` must reference a valid function.
- `flags` must only use bits 0..3.
- `reserved` must be 0.
- Duplicate import names (module + symbol) are rejected.
- Duplicate export names are rejected.
- Import entries map to function IDs at load time: `func_id = functions_count + import_index`.

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

### Error Convention (FFI)
- Host functions return normal values as specified by their signatures.
- On failure, host code must request a trap and return a sentinel error value:
  - `i32/i64`: return `-1` and trap.
  - `ref`: return null (`0xFFFFFFFF`) and trap.
  - `void`: trap directly.
- Imports marked with `can_trap` may legally trigger traps.

### Pinning / GC Interop Policy
- Raw pointers to VM-managed heap data are **not** exposed through FFI.
- Host code must use the Host API to read/write VM-managed data.
- No pinning in v0.1. If pinning is needed later, it must be explicit and versioned.

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


---
