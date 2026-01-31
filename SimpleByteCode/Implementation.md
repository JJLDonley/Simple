# Simple VM Implementation Plan

This document defines the full implementation plan for the Simple VM runtime and the testing strategy for the VM and all opcodes.

---

## 9) Pre-Freeze Plan (Primitives, ABI, FFI, Core Library)

### 9.1 Primitive Freeze
- [ ] Confirm VM primitive set: `i32/i64/f32/f64/ref` (+ `void` for signatures only).
- [ ] Add C-style type + opcode mapping table (storage + operator families).
- [ ] Lock VM type ID codes and version them.

#### C-Style Type + Opcode Mapping (Draft)
| C-Style Type | VM Storage Type | Opcode Families |
|-------------|------------------|-----------------|
| bool | i32 | BoolNot/BoolAnd/BoolOr, CmpEqI32 |
| char | i32 | ConstChar, StringGetChar |
| i8 | i32 | ConstI8, Inc/DecI8, NegI8 |
| u8 | i32 | ConstU8, Inc/DecU8 |
| i16 | i32 | ConstI16, Inc/DecI16, NegI16 |
| u16 | i32 | ConstU16, Inc/DecU16 |
| i32 | i32 | Add/Sub/Mul/Div/ModI32, Cmp*I32, BitwiseI32 |
| u32 | i32 | Add/Sub/Mul/Div/ModU32, Cmp*U32, ShiftI32 |
| i64 | i64 | Add/Sub/Mul/Div/ModI64, Cmp*I64, BitwiseI64 |
| u64 | i64 | Add/Sub/Mul/Div/ModU64, Cmp*U64, ShiftI64 |
| f32 | f32 | Add/Sub/Mul/DivF32, Cmp*F32 |
| f64 | f64 | Add/Sub/Mul/DivF64, Cmp*F64 |
| ref<T> | ref | RefEq/RefNe, IsNull, Load/StoreRef, Array/List ops |

### 9.2 ABI Freeze (SBC + Bytecode)
- [ ] Freeze opcode IDs, operand widths, stack effects, and trap conditions.
- [ ] Freeze SBC header fields, section IDs, table layouts, and alignment rules.
- [ ] Freeze call conventions: arg order, return slots, tailcall rules, `stack_max` meaning.
- [ ] Freeze const pool formats (string encoding, i128/u128 blobs, f32/f64 encoding).
- [ ] Define compatibility rules for header version/flags.

### 9.3 FFI ABI Freeze
- [ ] Finalize import/export tables (names, sig_id, flags).
- [ ] Finalize FFI flags and error/trap semantics.
- [ ] Define ref-handle ownership rules across boundary.
- [ ] Specify host API surface for ref/string/array/list access.

### 9.4 Core Library Contracts
- [ ] Define core library namespaces and signatures (no implementation yet).
- [ ] Decide which functions are intrinsic vs bytecode helpers.
- [ ] Lock error model (trap vs return code) per function family.

### 9.5 Freeze Gates (Tests)
- [ ] ABI validation tests (opcode IDs, header/section invariants).
- [ ] FFI table validation tests (bad names/sigs/flags).
- [ ] Intrinsic ID table validation tests (unknown/unsupported IDs).
- [ ] Cross-version compatibility test skeleton (header version/flags).

---

## 10) Detailed Implementation Plan (Pre-Freeze)

### Step 1: Primitive Freeze (VM Types)
Deliverables:
- Final VM primitive set locked to `i32/i64/f32/f64/ref` (+ `void`).
- VM type ID codes finalized and documented.
- C-style type mapping table confirmed (storage + op family).

Work:
- Add explicit VM type ID constants to SBC docs.
- Ensure verifier uses only these primitives as stack/local/global types.
- Update any signatures/metadata that still accept expanded types.
- Explicitly define struct layout rules (field order, alignment, padding) for FFI structs.

Tests:
- Loader rejects unknown VM type IDs.
- Verifier rejects opcodes with mismatched VM types.

### Step 2: Opcode + Bytecode ABI Freeze
Deliverables:
- Opcode IDs locked; operand widths + stack effects locked.
- Trap conditions documented (bounds, null, div-by-zero, type mismatches).
- Jmp/JmpTable encoding fixed (offsets, table layout).

Work:
- Freeze `opcode.h` and `SBC_OpCodes.md` to match.
- Add a check in loader to reject unknown opcodes (if not already).
- Freeze instruction size table for verifier.
- Add explicit “frozen semantics” section to `SBC_OpCodes.md` (operand widths, stack effects, traps).

Tests:
- Loader rejects invalid opcode values.
- Verifier rejects invalid operand widths or malformed instructions.

### Step 3: SBC Format Freeze
Deliverables:
- Header fields frozen (version, endian, flags, reserved).
- Section IDs and table layouts frozen.
- Alignment rules fixed (table + section alignment).

Work:
- Update `SBC_Headers.md`, `SBC_Sections.md`, `SBC_Metadata_Tables.md`.
- Lock const pool formats (string, i128/u128 blobs, f32/f64).

Tests:
- Existing loader negative tests updated to match frozen rules.
- Add “unknown section id” and “misaligned section” tests if missing.

### Step 4: Intrinsic ID Freeze
Deliverables:
- Intrinsic ID table finalized (IDs + signatures + trap rules).
- Debug/time/rand/io intrinsics stabilized.

Work:
- Intrinsic ID table is defined in `SimpleByteCode/SBC_ABI.md`.
- Define intrinsic IDs as constants in VM.
- Ensure `Intrinsic` opcode validates ID + signature.

Tests:
- Invalid intrinsic ID rejects at verify or runtime.
- Signature mismatch is rejected by verifier.

### Step 5: FFI ABI Freeze
Deliverables:
- Import/export table layout finalized.
- FFI flags and versioning finalized.
- Ref handle ownership rules defined.

Work:
- Use `SimpleByteCode/SBC_ABI.md` as the single source of truth for FFI tables.
- Define host API surface for ref/string/array/list access.
- Decide error propagation (trap code + message).
- Define OS-specific core library contracts in `SBC_ABI.md` (`core.os`, `core.fs`, `core.log`).
- Define concrete FFI error convention (return codes + trap behavior).
- Define pinning policy (explicitly allowed or explicitly forbidden).

Tests:
- Loader rejects malformed import/export tables.
- Verifier rejects call signatures not matching import sigs.

### Step 6: Core Library Contract (No Impl Yet)
Deliverables:
- Core library namespaces + signatures frozen.
- Error model per namespace fixed (trap vs return code).

Work:
- Enumerate core library functions that are NOT opcode-backed.
- Decide which are intrinsic vs bytecode helpers.
- Keep OS-specific contracts in `SBC_ABI.md` and enforce via import table.

Tests:
- Intrinsic ID table coverage for declared core functions.

### Step 7: Freeze Gates + Tag
Deliverables:
- All freeze-gate tests green.
- Create a freeze tag (e.g., `vm-freeze-v0.1`).

Work:
- Run full suite; add missing ABI/FFI tests.
- Final review of SBC docs vs VM behavior.

Tests:
- Full test suite pass.
- ABI validation tests pass.
- FFI table validation tests pass.
- Intrinsic ID table validation tests pass.
- Cross-version compatibility skeleton tests pass.

---

## 1) Goals

- Portable C++ VM that loads and executes SBC modules.
- IL-style verifier for safe execution and JIT eligibility.
- Tiered JIT (Tier 0 quick, Tier 1 optimizing).
- Tracing GC with clear root identification.
- Deterministic, reproducible execution for testing.

## 2) Non-Goals (v0.1)

- AOT compilation.
- Full optimizer pipeline.
- Full standard library runtime.
- OS-specific JIT backends (start with portable baseline).

---

## 3) High-Level Architecture

- **Loader**: Reads SBC header, section table, metadata tables, heaps, and CODE.
- **Verifier**: Validates structural and type safety before execution/JIT.
- **Interpreter**: Executes SBC bytecode in a typed stack VM.
- **Typed Runtime Core**: Operand stack, locals, and globals are untagged slots (no `ValueKind` at runtime); types are enforced by verifier + metadata.
- **JIT**:
  - Tier 0: fast, minimal optimizations.
  - Tier 1: optimized code for hot methods.
- **GC**: Tracing mark-sweep or generational collector.
- **Intrinsics**: Built-ins for IO and core library hooks.

---

## 4) Implementation Phases (VM)

### Phase 0: Foundations
- [DONE] Implement module loader with strict validation per SBC docs.
- [DONE] Implement metadata tables, string heap, blob heap decoding.
- [DONE] Implement error reporting with offsets and table indexes.
- [DONE] Validate DEBUG section header and line table bounds.

Acceptance:
- Loader rejects invalid headers and sections.
- Loader parses all tables and code correctly.

### Phase 1: Verifier
- [DONE] Implement instruction boundary validation.
- [DONE] Implement stack height tracking with merge checks.
- [DONE] Implement basic type checking for typed opcodes.
- [DONE] Validate jump targets and call signatures.
- [DONE] Verify against VM-level types (i32/i64/f32/f64/ref) rather than runtime tags.
- [DONE] Emit per-method local type info and stack maps for GC safepoints.
- [DONE] Emit ref bitmaps for locals/globals.

Acceptance:
- Invalid bytecode is rejected with clear diagnostics.
- Verified bytecode can be executed safely.

### Phase 2: Typed Runtime Refactor (No Tagged Values)
- [DONE] Replace `Value`/`ValueKind` with raw `Slot` storage for stack/locals/globals.
- [DONE] Remove runtime type checks; rely on verifier + debug asserts.
- [DONE] Add slot pack/unpack helpers (i32/i64/f32/f64/ref).
- [DONE] Update call frames to hold untagged locals.

Acceptance:
- Runtime contains no tagged values and passes existing opcode tests.
- Verified bytecode executes correctly without runtime type checks.

### Phase 3: Interpreter Core
- [DONE] Implement stack-based execution engine.
- [DONE] Support locals, globals, and call frames.
- [DONE] Locals arena to reduce per-call allocations.
- [DONE] Implement core opcode groups:
  - Control
  - Stack / Constants
  - Locals / Globals
  - Arithmetic / Bitwise (typed)
  - Comparisons
  - Boolean
  - Calls / Frames
- [DONE] Migrate opcode handlers to untagged slots.

Acceptance:
- Simple arithmetic programs run and return correct exit codes.

### Phase 4: Heap Objects + GC
- [DONE] Implement heap object headers and type ids.
- [DONE] Implement strings, arrays, lists, artifacts, closures.
- [DONE] Replace tag-based GC root scanning with ref bitmaps + stack maps.
- [DONE] Run GC only at safepoints with stack maps.
- [NEW] Two-phase GC roadmap (arena/scratch + young/old).

Acceptance:
- Allocations are tracked and reclaimed safely.
- Stress tests do not leak memory.

#### Phase 4b: Two-Phase GC Roadmap
- [NEW] Phase A (Scratch/Arena): add explicit short-lived arenas for transient allocations.
  - Scope-limited allocations (compiler/JIT/temp runtime helpers).
  - No-escape rule enforced by API (arena handles cannot be stored in heap objects or globals).
  - Bulk free on scope end; no GC scan needed.
- [NEW] Phase B (Young/Old Generational):
  - Young gen uses copying collection for fast allocation + compaction.
  - Old gen remains current mark-sweep heap.
  - Add promotion policy (survivor count/age threshold).
  - Add write barrier for old->young references (remembered set).
  - Keep stack maps/ref bitmaps as root sources for both generations.

Acceptance (Phase 4b):
- Scratch/arena allocations never escape; debug checks catch violations.
- Young-gen collections keep pause time low under allocation-heavy workloads.
- Old-gen stability remains (no regressions in existing sweep behavior).

### Phase 5: Extended OpCodes
- [DONE] Memory / Objects opcodes (field loads/stores, typeof, ref checks).
- [DONE] Arrays / Lists / Strings opcodes.
- [DONE] Array/List typed variants for I64/F32/F64/REF.
- [DONE] Conversions / Casts opcodes.
- [DONE] Intrinsics and syscalls.
- [DONE] Ensure all opcodes operate on untagged slots only.

Acceptance:
- Collections and string ops are correct and bounds-checked.

### Phase 6: Tiered JIT
- [DONE] Implement Tier 0 quick JIT for hot methods.
- [DONE] Add counters and hotness tracking.
- [DONE] Implement Tier 1 optimized JIT pass.
- [DONE] Add JIT fallback to interpreter on failure.
- [DONE] JIT execution uses untagged slots + verifier summaries (no tag checks).

Acceptance:
- Hot functions promote to Tier 1.
- JIT results match interpreter output.

### Phase 7: Tooling + Diagnostics
- [DONE] Line number mapping integrated into runtime trap errors.
- [DONE] Stack trace emission (function indices at minimum).
- [DONE] Trap errors include per-function PC offsets.
- [DONE] Trap errors include last opcode name and operand hints for CALL/JMP/JMP_TABLE.
- [DONE] Breakpoints and basic debug no-ops.
- [DONE] Profiling hooks.

Acceptance:
- Debug info produces correct line/column mapping.

---

## 5) Testing Strategy

### 5.1 Loader Tests
- Header validation tests (magic, version, flags, reserved fields).
- Section table tests (overlap, out-of-bounds, count mismatch).
- Table decoding tests (row sizes, indices).
- Heap decoding tests (string termination, blob bounds).

### 5.2 Verifier Tests
- Stack underflow and overflow cases.
- Jump target alignment and bounds.
- Merge point type mismatches.
- Local uninitialized use.
- Call signature mismatches.

### 5.3 Opcode Unit Tests

For each opcode:
- Encode a minimal bytecode snippet.
- Execute and assert stack effects and result.
- For invalid inputs, assert correct failure/trap.

Coverage targets:
- Arithmetic: signed/unsigned, overflow behavior, division by zero.
- Comparisons: equality and ordering.
- Memory: bounds checks and null checks.
- Lists: push/pop/insert/remove behavior.
- Strings: length, slice, concat correctness.

### 5.4 Integration Tests
- Full program execution with branching, loops, calls.
- Artifact instantiation and method calls.
- Collections and string operations in a single program.

### 5.5 Differential Tests
- Run bytecode in interpreter and JIT and compare results.
- Verify identical outputs across tiers.

### 5.6 Fuzzing
- Random bytecode generation with verification enabled.
- Mutation-based fuzzing on valid modules.
- Validate that verifier rejects unsafe code.

### 5.7 Performance Tests
- Hot loop benchmarks to validate tiering.
- Allocation-heavy workloads to validate GC.
- Measure JIT compile time vs runtime speedup.

### 5.8 Pre-Freeze Coverage Matrix
- Loader: section/table bounds, const pool integrity, DEBUG tables, signature param lists.
- Verifier: stack height/merge types, jump targets, call signature typing, locals/globals init.
- Runtime: traps for bounds/null/type errors, stack discipline, recursion, locals arena preservation.
- JIT: fallback safety, opcode-hot promotion, tiering counters, differential interpreter vs JIT.
- GC: root scanning via stack maps/bitmaps, stress allocation, mark/sweep correctness.

### 5.9 Pre-Freeze ABI Checklist (VM + SBC)
- Freeze opcode IDs and semantics (operand widths, stack effects, trap conditions).
- Freeze SBC header fields, section IDs, table layouts, and alignment rules.
- Freeze call conventions: arg order, return slots, tailcall rules, stack_max meaning.
- Freeze type ID mapping for VM primitive types (i32/i64/f32/f64/ref) and reference kind rules.
- Freeze const pool formats (string encoding, i128/u128 blob sizes, f32/f64 encoding).
- Freeze intrinsic/syscall ABI: numbering, signatures, error handling, and versioning.
- Freeze GC safepoint contract: where safepoints occur and how stack maps are encoded.
- Define module compatibility rules (versioning, feature flags, reserved fields behavior).

### 5.10 Core Library + OS Layer Roadmap
Phase A: VM Core Library (no OS)
- String helpers: concat/slice/len parity with opcodes; UTF-16/UTF-8 policy.
- Array/list helpers: bounds-safe wrappers, bulk operations (fill/copy).
- Numeric helpers: abs/min/max/clamp, conversions; define overflow policy.
- Error model: trap vs error code vs exception (if any).

Phase B: OS Abstraction Layer (portable)
- File IO: open/read/write/close with platform-neutral flags.
- Time: monotonic clock + wall clock (explicit units).
- Random: seeded RNG and OS entropy hook.
- Environment: args, env, cwd.
- Logging: stdout/stderr abstraction.

Phase C: Native ABI + FFI (optional, post-freeze)
- Stable calling convention for host functions.
- Memory ownership rules for ref types across boundary.
- Versioned import/export tables in SBC.

### 5.11 Intrinsic Libraries + FFI ABI (Concrete Tables)

Intrinsic Calling Convention (Fixed)
- Intrinsic ID: `u32`, stable, global namespace (no gaps for now).
- Signature: defined by VM primitive types only (`i32/i64/f32/f64/ref/void`).
- Error model: intrinsic returns `Trap` with message; no exceptions.
- Side effects: only as documented per intrinsic.

VM Primitive Type Codes (for signatures)
- `0 = void`
- `1 = i32`
- `2 = i64`
- `3 = f32`
- `4 = f64`
- `5 = ref`

Intrinsic Table (v0.1, stable IDs)
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

FFI ABI (Host Interop, Concrete Tables)

Import Table Layout (per entry)
- `module_name_str` (u32, string heap offset)
- `symbol_name_str` (u32, string heap offset)
- `sig_id` (u32, signature table index)
- `flags` (u32)

Export Table Layout (per entry)
- `symbol_name_str` (u32, string heap offset)
- `func_id` (u32, functions table index)
- `flags` (u32)
- `reserved` (u32)

FFI Flags (import/export)
- `0x0001`: can_trap (host may return trap)
- `0x0002`: pure (no side effects)
- `0x0004`: no_gc (host will not allocate or trigger GC)
- `0x0008`: allow_ref (host accepts/ref returns ref handles)

FFI Calling Convention
- Args passed in VM order, stack top = last arg.
- Returns: 0 or 1 slot (per signature).
- Ref values are opaque handles; host must not assume pointer layout.
- Host API must expose: retain/release ref, read string, list/array ops.
- ABI version must be declared in SBC header flags or dedicated field.

Host API details moved to `SimpleByteCode/SBC_ABI.md`.

---

## 6) Test Infrastructure

- Test runner that loads SBC modules from a `tests/` folder.
- Golden outputs for deterministic results.
- Bytecode builder utilities to construct test modules.
- CI-friendly execution mode (no external dependencies).

---

## 7) Definition of Done (VM)

- Loader and verifier conform to SBC docs.
- Interpreter passes all opcode tests.
- JIT tiering works and is validated by differential tests.
- GC stable under stress tests.
- Debug info and diagnostics are correct.
- Runtime uses untagged slots (no `ValueKind` or runtime type tags).

---

## 8) Current Status (Living Notes)

- VM interpreter, verifier, and core opcode tests are in place and expanding.
- Heap objects and basic GC root marking are implemented (mark/sweep pass).
- Arrays, lists, strings, and object fields have runtime and negative tests.
- Added coverage for remaining core arithmetic/compare/bitwise opcodes and list set variants.
- Added negative verify coverage for array/list set value mismatches across typed variants.
- Added runtime trap coverage for typed list set null/out-of-bounds/negative index cases.
- Added runtime trap coverage for typed array set null/out-of-bounds/negative index cases.
- Added runtime trap coverage for typed array/list get and list pop/insert/remove cases.
- Added JIT fallback coverage for typed array/list ops to ensure safe interpreter fallback.

---
