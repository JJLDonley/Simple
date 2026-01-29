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
