# Simple VM IR (Design Requirements)

This document defines the Simple VM intermediate representation (IR) used to emit SBC bytecode.

---

## 1. Goals

- Provide a typed, VM-level emission layer similar to CLR IL.
- Emit valid SBC bytecode with verified stack behavior.
- Remain language-agnostic (compiler enforces language semantics).

---

## 2. IR Model (v0.1)

- **Stack-based** instructions.
- **VM types only**: `i32/i64/f32/f64/ref` and `void` for signatures.
- IR is a **structured emitter** (labels + fixups) that writes SBC bytecode.
- IR does **not** encode SSA or high-level AST semantics.

---

## 3. Instruction Form

IR instructions map **1:1** to SBC opcodes.

- Zero-operand ops: `ADD_I32`, `RET`, `POP`, etc.
- Fixed operands: `ENTER u16`, `CALL idx,u8`, `JMP i32`, `CONST_I32 u32`, etc.

---

## 4. Module/Function Scope

IR is emitted per function and then assembled into an SBC module:

- Each IR function emits a linear bytecode stream.
- Module tables (types/fields/methods/sigs/globals/functions) are supplied by the language compiler.
- The IR emitter does **not** infer signatures or types; it only emits opcodes.

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
- Constants: `EmitConstI32/I64/F32/F64`.
- Calls: `EmitCall`, `EmitCallIndirect`, `EmitTailCall`.
- Control-flow: `CreateLabel`, `BindLabel`, `EmitJmp/JmpTrue/JmpFalse`.
- Finalize: `Finish(out_code, out_error)`.

---

## 10. Validation

- IR builder enforces label binding and fixup resolution.
- Runtime correctness is verified by existing SBC verifier.

---

## 11. Future Extensions

- SSA-form IR (optional).
- Optimization passes before bytecode emission.
- IR-level diagnostics and source mapping.
