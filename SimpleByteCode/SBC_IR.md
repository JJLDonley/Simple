# Simple VM IR (SIR) Language

This document defines the Simple VM intermediate representation (SIR) used to emit SBC bytecode. It is intended
for language implementers who want a stable, VM-typed IR target.

---

## 1. Goals

- Provide a typed, VM-level IR target similar to CLR IL (stack-based, opcode-centric).
- Emit valid SBC bytecode with verified stack behavior.
- Remain language-agnostic (compiler enforces language semantics).
- Keep the IR easy to generate from compilers (no SSA requirement in v0.1).

---

## 2. SIR Model (v0.1)

- **Stack-based** instruction stream.
- **VM types only**: `i32/i64/f32/f64/ref` and `void` for signatures.
- SIR is a **structured emitter** (labels + fixups) that writes SBC bytecode.
- SIR does **not** encode SSA or high-level AST semantics.

SIR exists in two forms:
- **Programmatic IR builder** (C++ API) used by compilers.
- **Optional text form** (for debugging/tests), parsed and lowered into the builder.

---

## 3. Instruction Form

SIR instructions map **1:1** to SBC opcodes.

- Zero-operand ops: `ADD_I32`, `RET`, `POP`, etc.
- Fixed operands: `ENTER u16`, `CALL idx,u8`, `JMP i32`, `CONST_I32 u32`, etc.

---

## 4. Module/Function Scope

SIR is emitted per function and then assembled into an SBC module:

- Each IR function emits a linear bytecode stream.
- Module tables (types/fields/methods/sigs/globals/functions) are supplied by the language compiler.
- The IR emitter does **not** infer signatures or types; it only emits opcodes and resolves labels.

**Compiler responsibility:** supply all metadata tables and ensure indices used in SIR match those tables.

---

## 5. Stack Discipline

IR is stack-based and must obey SBC verifier rules:

- Stack depth never negative.
- Stack height + VM types match at control-flow joins.
- Opcodes consume/produce fixed VM types (`i32/i64/f32/f64/ref`).
- Return value type must match method signature.

The IR emitter does not bypass verification; the SBC verifier remains authoritative.

---

## 6. Labels + Fixups

- IR supports labels for control-flow emission.
- `JMP/JMP_TRUE/JMP_FALSE` are emitted with relative `i32` offsets.
- Fixups are resolved at finalize time once label offsets are known.
- A label can be bound once; re-binding is an error.

---

## 7. Emission Rules

- Emitted bytecode must follow frozen SBC opcode rules.
- All operands are little-endian.
- Jump targets are relative to the **next instruction** (same as VM runtime).
- Unbound labels are an error at finalize time.

---

## 8. Error Model

- IR builder returns a **compile-time error** for:
  - Unbound labels.
  - Invalid label binding (out-of-range or duplicate bind).
- Runtime correctness is verified by the SBC verifier and loader.

---

## 9. Builder API (v0.1)

Required emitter operations:

- Basic ops: `EmitOp(opcode)`.
- Locals: `EmitEnter(u16 locals)`.
- Constants: `EmitConstI8/I16/I32/I64/U8/U16/U32/U64/F32/F64/Bool/Char/String/Null`.
- Calls: `EmitCall`, `EmitCallIndirect`, `EmitTailCall`.
- Control-flow: `CreateLabel`, `BindLabel`, `EmitJmp/JmpTrue/JmpFalse`, `EmitJmpTable`.
- Stack ops: `EmitPop/Dup/Dup2/Swap/Rot`.
- Arithmetic (signed): `EmitAdd/Sub/Mul/Div/Mod` for I32/I64.
- Arithmetic (unsigned): `EmitAdd/Sub/Mul/Div/Mod` for U32/U64.
- Arithmetic (float): `EmitAdd/Sub/Mul/Div` for F32/F64.
- Comparisons: `EmitCmpEq/Ne/Lt/Le/Gt/Ge` for I32/I64/U32/U64/F32/F64.
- Bitwise: `EmitAnd/Or/Xor/Shl/Shr` for I32/I64.
- Unary: `EmitNeg/Inc/Dec` for I32/I64.
- Conversions: `EmitConvI32ToI64/I32ToF32/I32ToF64/I64ToI32/F32ToI32/F64ToI32/F32ToF64/F64ToF32`.
- References/objects: `EmitConstNull`, `EmitNewObject`, `EmitNewClosure`, `EmitLoadField`, `EmitStoreField`,
  `EmitTypeOf`, `EmitIsNull`, `EmitRefEq`, `EmitRefNe`, `EmitLoadUpvalue`, `EmitStoreUpvalue`.
- Arrays/lists/strings: `EmitNewArray/ArrayLen/ArrayGet*/ArraySet*`, `EmitNewList/ListLen/ListGet*/ListSet*/ListPush*/ListPop*/ListInsert*/ListRemove*/ListClear`,
  `EmitStringLen/StringConcat/StringGetChar/StringSlice`.
- Diagnostics/intrinsics: `EmitCallCheck`, `EmitIntrinsic`, `EmitSysCall`.
- Finalize: `Finish(out_code, out_error)`.

---

## 10. SIR Text Form (Optional, v0.1)

The text form is intentionally minimal and **not** required for production compilers. It is used for tests and
debugging. The parser lowers directly into the IR builder.

### 10.1 File Structure

```
module <name>
func <name> locals=<u16> stack=<u16> sig=<sig_id>
entry <label>
  <instr>
  ...
endfunc
```

- `sig=<sig_id>` refers to the signature table row supplied by the compiler.
- `locals=<u16>` and `stack=<u16>` are required for verifier constraints.

### 10.2 Labels

```
label <name>
jmp <label>
jmpt <label>    // jmp_true
jmpf <label>    // jmp_false
```

### 10.3 Constants (Examples)

```
const.i32 123
const.i64 1000
const.u32 42
const.f32 1.5
const.f64 2.0
const.bool 1
const.null
```

### 10.4 Core Arithmetic (Examples)

```
add.i32
sub.i64
mul.f32
div.u64
```

### 10.5 Calls (Examples)

```
call <method_id> <arg_count>
call.indirect <sig_id> <arg_count>
tailcall <method_id> <arg_count>
```

### 10.6 Structured Jump Table (Example)

```
jmptable <default_label> <case_count> <label0> <label1> ... <labelN>
```

**Note:** The current text parser supports a subset of opcodes. Use the programmatic IR builder for full coverage.

---

## 11. Validation

- IR builder enforces label binding and fixup resolution.
- Runtime correctness is verified by existing SBC verifier.

---

## 12. Imports + FFI Referencing (v0.1)

- FFI is modeled as **imports**, not special opcodes.
- The IR emitter emits `CALL/CALL_INDIRECT/TAILCALL` against a method/sig that is mapped to an import.
- The **compiler supplies** the Imports table (module name, symbol name, and signature id) and the Sig table.
- At load time, imports are resolved to host functions; at runtime, calls go through the same call path.

Practical flow:

1. Compiler defines `SigSpec` for the host function.
2. Compiler emits an Import row pointing at `module_name` + `symbol_name` + `sig_id`.
3. IR emits a `CALL` targeting that function (via method/function table index).

Intrinsics/SysCalls remain VM-owned (`EmitIntrinsic/EmitSysCall`) and are not part of FFI.

---

## 13. Compiler Responsibilities (Checklist)

When targeting SIR, the compiler must:

1. **Define metadata tables** (Types, Fields, Methods, Signatures, Globals, Functions, Imports/Exports).
2. **Lower to VM types** (`i32/i64/f32/f64/ref`) and select the matching opcode.
3. **Emit correct stack behavior** (stack discipline and merge consistency).
4. **Provide signature IDs** and use them in call opcodes.
5. **Provide correct indices** for globals, fields, types, strings, and methods.
6. **Set stack_max/local_count** for each function (verifier enforced).

---

## 14. Status (Living Notes)

- Implemented: label-based IR builder that emits SBC bytecode, JmpTable fixups, and a broad set of opcode helpers.
- Implemented: IR→SBC module packing via `IrModule`/`CompileToSbc` with tables supplied by the compiler.
- Implemented: extensive IR→SBC tests covering control-flow, stack ops, arithmetic, compares, arrays/lists/strings, refs, and closures.
- Implemented: minimal typed IR text parser/lowerer (v0.1 subset) that lowers into the IR builder.
- Planned: a true higher-level IR layer (separate from opcode emission) if needed, with its own lowering pass into the IR builder.

## 15. Future Extensions

- SSA-form IR (optional).
- Optimization passes before bytecode emission.
- IR-level diagnostics and source mapping.
