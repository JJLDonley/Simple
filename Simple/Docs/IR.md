# Simple::IR (Authoritative)

## Scope

`Simple::IR` owns SIR ingestion and lowering to SBC.

This module is the bridge between language-level emission and bytecode runtime contract.

## Component Design

### 1) SIR Front-End

Implemented primarily in `Simple/IR/src/ir_lang.cpp`:

- tokenization/parsing of SIR text
- section parsing (`types`, `sigs`, `consts`, `imports`, `globals`, `func`)
- symbol/name resolution across sections

### 2) IR Builder

Implemented in `Simple/IR/src/ir_builder.cpp`:

- structured opcode emission
- label binding and fixup resolution
- instruction stream finalization

### 3) Lowering to SBC

- resolves names to table IDs
- emits tables (types/fields/methods/sigs/globals/functions/imports/exports)
- emits code bytes with fixed operand widths
- ensures output matches loader/verifier constraints

## SIR Program Design

A valid SIR module is section-oriented:

1. optional metadata sections (`types`, `sigs`, `consts`, `imports`, `globals`)
2. one or more `func` blocks
3. `entry` declaration

### Function Form

Each function declares:

- `locals=<u16>`
- `stack=<u16>`
- `sig=<id|name>`
- body instructions

### Labels and Control Flow

- labels are single-function scope
- jump offsets are fixed at finalize time
- unresolved/duplicate labels are errors

## Type and Signature Design

### Declared SIR Types

SIR accepts richer names (including small ints/bool/char/string/user types), then lowers to VM-relevant table rows.

### Signature Constraints

- callsites must match signature param count and types
- return shape must match signature return type
- indirect calls require matching signature row

### Globals

- globals are declared in `globals:` with type and optional init binding
- init constant handling follows current IR const-kind constraints

## Emission Contract

- every emitted opcode must exist in `OpCode` enum
- operand widths must match `GetOpInfo`
- emitted table indices must be in range
- generated module must pass `Simple::Byte` loader + verifier

## Validation Rules (Authoritative)

SIR lowering rejects:

- unknown section kinds/row forms
- unresolved names (types/sigs/consts/functions/globals)
- malformed constants or unsupported const kinds
- mismatched function metadata (`locals`/`sig`/body expectations)
- invalid jump targets/label references

## Supported SIR Surface (Current)

- arithmetic/compare/bool/bitwise opcode families
- locals/globals/upvalues
- call/call-indirect/tailcall
- object/field/ref operations
- arrays/lists/strings
- intrinsic/syscall op emission

Unsupported or constrained paths fail explicitly with diagnostics.

## Files

- `Simple/IR/src/ir_lang.cpp`
- `Simple/IR/src/ir_builder.cpp`
- `Simple/IR/include/ir_lang.h`
- `Simple/IR/include/ir_builder.h`

## Verification Commands

- `./Simple/build.sh --suite ir`
- `./Simple/build.sh --suite all`

## Legacy Migration Notes

`Simple/Docs/legacy/SBC_IR.md` is non-authoritative historical reference.
