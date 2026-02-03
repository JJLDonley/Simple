# Simple Modules

This document defines each Simple module in a consistent format.

---

## Module: Simple::Byte

**Scope**  
SBC file format, encoding rules, metadata tables, opcode list, loader rules, and verifier rules.

**How It Works**  
The Byte module defines the SBC binary layout and rules for valid modules. The loader parses headers,
section tables, metadata tables, heaps, and code. The verifier enforces structural and type safety
before execution or JIT.

**What Works (Current)**  
- SBC headers/sections/encoding rules implemented and validated.
- Metadata tables and heaps decoded with strict bounds checks.
- Verifier enforces stack/type safety and control-flow validity.

**Implementation Plan**  
1) Header + section table parsing and validation.  
2) Metadata + heap decoding.  
3) Verifier rules (stack/type/CF).  
4) Diagnostics and error context (table/offset/line).  

**Future Plan**  
- Extend metadata only if new VM features require it.
- Keep ABI-compatible changes behind version bumps.

Docs:  
- `Simple/Docs/SBC_Headers.md`  
- `Simple/Docs/SBC_Encoding.md`  
- `Simple/Docs/SBC_Sections.md`  
- `Simple/Docs/SBC_Metadata_Tables.md`  
- `Simple/Docs/SBC_OpCodes.md`  
- `Simple/Docs/SBC_Rules.md`

---

## Module: Simple::VM

**Scope**  
Runtime execution engine, heap/GC, JIT, and runtime diagnostics.

**How It Works**  
The VM executes verified SBC bytecode using an untagged slot runtime (types enforced by verifier).
Heap objects are managed by GC, and JIT tiers can optimize hot methods.

**What Works (Current)**  
- Interpreter, verifier integration, and typed slot runtime.
- Heap objects (strings/arrays/lists/closures) and GC safepoints.
- Tiered JIT path and runtime trap diagnostics.

**Implementation Plan**  
1) Core runtime and call frames.  
2) Heap + GC with ref bitmaps/stack maps.  
3) Extended opcodes (objects/arrays/lists/strings).  
4) Tiered JIT and diagnostics.  

**Future Plan**  
- Optional GC enhancements (arena/young/old) after v0.1 stability.
- Further JIT optimization passes as needed.

Docs:  
- `Simple/Docs/SBC_Runtime.md`  
- `Simple/Docs/SBC_Debug.md`  
- `Simple/Docs/SBC_ABI.md`

---

## Module: Simple::IR

**Scope**  
SIR text format and IR compiler that lowers SIR → SBC.

**How It Works**  
SIR is a typed, text-based IR with optional metadata tables for name resolution. The IR compiler
parses SIR, validates types/labels/metadata, and emits SBC via the bytecode emitter.

**What Works (Current)**  
- SIR text grammar and parsing.  
- Metadata tables (types/sigs/consts/imports/globals/upvalues).  
- Name resolution + validation and SBC emission.  
- Line-aware diagnostics and perf harness for .sir programs.  

**Implementation Plan**  
1) Parser + tokenizer.  
2) Metadata tables + name resolution.  
3) Validation + lowering to SBC.  
4) Diagnostics and perf harness.  

**Future Plan**  
- Optional authoring ergonomics (includes/macros) if needed.
- Additional metadata conveniences only if VM requires it.

Docs:  
- `Simple/Docs/SBC_IR.md`

---

## Module: Simple::CLI

**Scope**  
Command-line runners and developer tools (test runners, perf harness, SIR runner).

**How It Works**  
CLI entry points use the IR compiler and VM runtime to run SIR text and SBC modules, with
diagnostics and perf reporting.

**What Works (Current)**  
- Test runners and perf harness.  
- SIR runner for IR text → SBC → VM execution.  

**Implementation Plan**  
1) Stable CLI arguments and help text.  
2) Diagnostics flags and output control.  

**Future Plan**  
- Dedicated `simplevm` runtime CLI once v0.1 freezes.

Docs:  
- `Simple/VM/README.md`

---

## Module: Simple::Tests

**Scope**  
Unit tests, negative tests, and perf programs for VM and IR.

**How It Works**  
Tests are organized by module (core/IR/JIT). SIR perf programs provide real workload coverage.

**What Works (Current)**  
- Split test suites by module.  
- SIR perf suite with real program coverage.  
- Negative tests for loader/verifier/runtime traps.  

**Implementation Plan**  
1) Expand coverage for IR and bytecode edge cases.  
2) Add perf regression baselines after stability.  

**Future Plan**  
- CI integration and perf regression gating.

Docs:  
- `Simple/Docs/Sprint.md`

---

## Module: Simple::Lang (Future Consumer)

**Scope**  
Language frontend that targets SIR.

**How It Works**  
A language compiler lowers source → SIR text (or direct emitter API) and relies on the VM for
verification and execution.

**What Works (Current)**  
- Not implemented (intentional).

**Implementation Plan**  
1) Build Simple language front-end targeting SIR.  
2) Layer standard library using ABI/FFI.  

**Future Plan**  
- Full language toolchain and packaging.

Docs:  
- `Simple/Docs/Simple_Programming_Language_Document.md`  
- `Simple/Docs/Simple_Implementation_Document.md`
