# Simple VM Implementation Plan

This document defines the full implementation plan for the Simple VM runtime and the testing strategy for the VM and all opcodes.

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

### 5.11 Intrinsic Libraries + FFI ABI Outline
Intrinsic Libraries (VM-owned)
- `core.string`: len/concat/slice/get_char, encoding rules, error/trap behavior.
- `core.array`: len/get/set, bounds/null policy, bulk ops (fill/copy) if added.
- `core.list`: len/get/set/push/pop/insert/remove/clear, bounds/null policy.
- `core.math`: abs/min/max/clamp, bit ops, conversions (explicit only).
- `core.debug`: trap, breakpoint, line/profile hooks (no side effects).
- `core.gc`: optional hooks (force_collect, stats) behind feature flag.

Intrinsic Calling Convention
- Intrinsic IDs are stable integers mapped to `core.*` namespaces.
- Signature is defined in SBC metadata (param/return types).
- Intrinsic failures use `Trap` with diagnostic string; no exceptions.
- Intrinsics never mutate VM state outside explicit parameters unless documented.

FFI ABI (Host Interop)
- Import table: `(module_name, symbol_name, sig_id, flags)` entries.
- Export table: `(symbol_name, func_id)` entries for host lookup.
- Calling convention: args passed by value in VM order; return in a single slot.
- Ref types: host receives opaque handles; ownership rules defined per function.
- Memory: no raw pointer exposure; host uses provided APIs to read/write.
- Errors: host may return a trap code + message; VM propagates as trap.
- Versioning: module declares required ABI version + feature flags.

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
