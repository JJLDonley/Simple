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
- **JIT**:
  - Tier 0: fast, minimal optimizations.
  - Tier 1: optimized code for hot methods.
- **GC**: Tracing mark-sweep or generational collector.
- **Intrinsics**: Built-ins for IO and core library hooks.

---

## 4) Implementation Phases (VM)

### Phase 0: Foundations
- Implement module loader with strict validation per SBC docs.
- Implement metadata tables, string heap, blob heap decoding.
- Implement error reporting with offsets and table indexes.

Acceptance:
- Loader rejects invalid headers and sections.
- Loader parses all tables and code correctly.

### Phase 1: Verifier
- Implement instruction boundary validation.
- Implement stack height tracking with merge checks.
- Implement basic type checking for typed opcodes.
- Validate jump targets and call signatures.

Acceptance:
- Invalid bytecode is rejected with clear diagnostics.
- Verified bytecode can be executed safely.

### Phase 2: Interpreter Core
- Implement stack-based execution engine.
- Support locals, globals, and call frames.
- Implement core opcode groups:
  - Control
  - Stack / Constants
  - Locals / Globals
  - Arithmetic / Bitwise (typed)
  - Comparisons
  - Boolean
  - Calls / Frames

Acceptance:
- Simple arithmetic programs run and return correct exit codes.

### Phase 3: Heap Objects + GC
- Implement heap object headers and type ids.
- Implement strings, arrays, lists, artifacts, closures.
- Implement tracing GC (mark-sweep or generational).

Acceptance:
- Allocations are tracked and reclaimed safely.
- Stress tests do not leak memory.

### Phase 4: Extended OpCodes
- Memory / Objects opcodes (field loads/stores, typeof, ref checks).
- Arrays / Lists / Strings opcodes.
- Conversions / Casts opcodes.
- Intrinsics and syscalls.

Acceptance:
- Collections and string ops are correct and bounds-checked.

### Phase 5: Tiered JIT
- Implement Tier 0 quick JIT for hot methods.
- Add counters and hotness tracking.
- Implement Tier 1 optimized JIT pass.
- Add JIT fallback to interpreter on failure.

Acceptance:
- Hot functions promote to Tier 1.
- JIT results match interpreter output.

### Phase 6: Tooling + Diagnostics
- Line number mapping and debug table usage.
- Breakpoints and basic stack traces.
- Profiling hooks.

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

---

## 8) Current Status (Living Notes)

- VM interpreter, verifier, and core opcode tests are in place and expanding.
- Heap objects and basic GC root marking are implemented (mark/sweep pass).
- Arrays, lists, strings, and object fields have runtime and negative tests.
