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

## 4) Modules, Docs, Goals, Checklists, Future, Non-Goals

Each module follows the same format:
- **Docs**: source-of-truth documents.
- **Goals**: what the module must achieve for v0.1.
- **Checklist**: detailed, itemized work (with completion).
- **Future**: items explicitly deferred beyond v0.1.
- **Non-Goals**: out of scope for now.

### 4.1 Module: Simple::Byte (SBC Format, Loader, Verifier)

Docs:
- `Simple/Docs/SBC_Headers.md`
- `Simple/Docs/SBC_Encoding.md`
- `Simple/Docs/SBC_Sections.md`
- `Simple/Docs/SBC_Metadata_Tables.md`
- `Simple/Docs/SBC_OpCodes.md`
- `Simple/Docs/SBC_Rules.md`

Goals (v0.1):
- A fully specified SBC format with a strict loader and verifier.
- Frozen opcode IDs, operand widths, and stack effects.
- Clear diagnostics for invalid modules.

Checklist:
- [x] Parse/validate headers and section table.
- [x] Decode metadata tables + heaps with strict bounds checks.
- [x] Verify instruction boundaries, stack heights, and type rules.
- [x] Validate jump targets and call signatures.
- [x] Freeze opcode IDs/operand widths/stack effects for v0.1.
- [x] Loader rejects unknown opcodes and malformed instructions.
- [x] Diagnostics include offsets, table indices, and opcode names.

Future:
- Metadata extensions only when new VM features require them.

Non-Goals:
- Breaking SBC ABI without version bump.

### 4.2 Module: Simple::VM (Runtime + GC + JIT)

Docs:
- `Simple/Docs/SBC_Runtime.md`
- `Simple/Docs/SBC_Debug.md`
- `Simple/Docs/SBC_ABI.md` (ABI/FFI surface)

Goals (v0.1):
- Untagged slot runtime with verifier-enforced typing.
- Interpreter + tiered JIT parity.
- GC with correct root scanning (stack maps/bitmaps).
- ABI/FFI tables validated at load time.

Checklist:
- [x] Untagged slot runtime (no ValueKind tags).
- [x] Interpreter covers all core opcodes.
- [x] Heap objects (string/array/list/artifact/closure).
- [x] GC roots via stack maps + ref bitmaps at safepoints.
- [x] JIT tiering with interpreter fallback.
- [x] Trap diagnostics include opcode + PC + method context.
- [x] Validate IMPORTS/EXPORTS tables and signatures.
- [x] Emit/import-call strategy for externs at runtime (FFI plumbing).

Future:
- Generational GC (young/old) and advanced optimizations.
- Full JIT optimizations beyond tier 1.

Non-Goals:
- Unsafe pointers or raw host memory access.

### 4.3 Module: Simple::IR (SIR Text + IR Compiler)

Docs:
- `Simple/Docs/IR.md`
- `Simple/Docs/SBC_IR.md` (legacy reference)

Goals (v0.1):
- Text IR that lowers to SBC with strict validation.
- Metadata tables for named resolution and diagnostics.

Checklist:
- [x] SIR text grammar + tokenizer.
- [x] Metadata tables (types/sigs/consts/imports/globals/upvalues).
- [x] Name resolution + type validation.
- [x] Lowering to SBC via emitter.
- [x] Line-aware diagnostics.
- [x] Perf harness for .sir programs.

Future:
- Optional IR authoring ergonomics (includes/macros).

Non-Goals:
- SSA or optimizer-level IR.

### 4.4 Module: Simple::CLI (Runner + Tools)

Docs:
- `Simple/Docs/CLI.md`

Goals (v0.1):
- Compile/run/check Simple and SIR.
- Optional static/dynamic embedded executables.
- Clear error output for users.

Checklist:
- [x] `simplevm run/build/check/emit` commands.
- [x] Build with `-d/--dynamic` and `-s/--static`.
- [x] Error format `error[E0001]: ...` with line/column.
- [x] Range highlights in diagnostics.
- [x] Dedicated `simple` CLI front-end and subcommands.

Future:

Non-Goals:
- Full package manager or LSP integration.

### 4.5 Module: Simple::Tests

Docs:
- `Simple/Docs/Sprint.md`

Goals (v0.1):
- Reliable unit tests + .simple fixtures.
- Negative tests for parser/semantic/runtime errors.
- Perf harness for IR/SIR.

Checklist:
- [x] Core/IR/JIT test suites split by module.
- [x] SIR perf suite with real programs.
- [x] .simple fixtures + negative fixtures.
- [x] CLI tests for build/run/check/emit.

Future:
- Perf baselines and regression thresholds.

Non-Goals:
- Exhaustive fuzzing in v0.1.

### 4.6 Module: Simple::Lang (Language Front-End)

Docs:
- `Simple/Docs/Lang.md` (authoritative spec)

Goals (v0.1):
- Full language front-end to SIR text.
- Strict typing + mutability checks per spec.
- IO.print/println lowering via intrinsics.
- Standardized core library surface with reserved imports.

Checklist:
1) Lexer
- [x] Keywords + literals + operators + comments.
- [x] Line/column tracking + EOF.

2) Parser
- [x] Declarations (var/proc/artifact/module/enum).
- [x] Types (primitives/arrays/lists/proc/user-defined).
- [x] Generics parsing.
- [x] Statements/expressions with precedence.
- [x] Imports and extern declarations.
- [x] Error recovery.

3) AST
- [x] Full node coverage + spans.
- [x] Generic type nodes.
- [x] Import/Extern nodes.

4) Semantic Analysis
- [x] Scope/symbol resolution.
- [x] Mutability and type checking.
- [x] Return path checking.
- [x] Array/list indexing validation.
- [x] Import/extern validation.

5) SIR Emission
- [x] Emit expressions, control flow, arrays/lists, artifacts, methods, Fn.
- [x] Default returns and locals/globals.
- [x] Emit extern import tables and FFI call sites.

6) Diagnostics
- [x] Error format + line/column.
- [x] Range highlights.

7) CLI Integration
- [x] simplevm compile/run/check Simple source.

8) Standard Library + Imports
- [x] Standardized library layout and naming (core + optional modules).
- [x] Reserved import paths for core libraries (Math, IO, Time, File, etc).
- [x] Import resolver maps reserved names to core library modules before filesystem/relative lookups.

Future:
- Full runtime wiring of standard library modules.
- Diagnostics range highlights and UX polish.
- Expanded FFI surface and host bindings.

Non-Goals:
- Pattern matching, unsafe pointers, full package/import system.

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
- Standardized SBC emitter helpers and migrated test fixtures and `gen_sbc` to use `sbc_emitter.h`.
- Added IR→SBC compiler API (`ir_compiler`) and wired IR tests through it.
- Added IR→SBC golden comparisons using `sbc_emitter` (no manual SBC bytes).
- Extended IR emitter API (locals/globals/ret/stack ops) and added IR→SBC tests for locals and calls.
- Added IR emitter support for bool consts and added IR→SBC tests for globals, stack ops, and conditional branches.
- Added IR emitter support for i32 comparisons and boolean ops with IR→SBC tests.
- Added IR emitter conversion ops and IR→SBC tests for i32↔i64 and i32↔f64 paths.
- Added IR emitter float arithmetic/conversion ops and IR→SBC tests for f32/f64 paths.
- Added IR emitter i32 bitwise/shift ops and IR→SBC tests.
- Added IR→SBC tests for call_indirect and tailcall paths.
- Added IR emitter array/list ops (i32) and IR→SBC tests for array/list paths.
- Added IR emitter string ops and IR→SBC test for string concat/len path.
- Added IR emitter `EmitConstString` and IR→SBC test using const-pool-backed strings.
- Added IR→SBC tests for string get-char and string slice paths.
- Added IR emitter ref ops (IsNull/RefEq/RefNe) and IR→SBC test.
- Added IR emitter field/object ops (NewObject/LoadField/StoreField/TypeOf) and IR→SBC tests.
- Added IR→SBC tests for field/object and typeof paths.
- Added IR emitter typed array/list ops and IR→SBC tests for I64/F32/Ref containers.
- Added IR→SBC tests for array/list F64 and ref array paths.
- Added IR→SBC tests for array F32 and list I64 paths.
- Added IR→SBC tests for array/list length ops.
- Added IR emitter list insert/remove/clear ops and IR→SBC tests.
- Added IR→SBC test for list get/set path.
- Added IR→SBC tests for array get/set (F32 and Ref).
- Added IR→SBC tests for list get/set (F32 and Ref).
- Added IR→SBC tests for list get/set (I64 and F64).

## 9) Freeze Status (v0.1)

- Freeze tag: `vm-freeze-v0.1`
- All Phase 9 pre-freeze items completed.
- Full test suite passed at freeze gate.

---
