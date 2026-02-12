# Simple::IR (API)

`Simple::IR` owns SIR parsing and lowering to SBC.

## Supported
- SIR parsing with section-oriented structure.
- `types`, `sigs`, `consts`, `imports`, `globals`, `func`, and `entry` sections.
- Label resolution and fixups within functions.
- Deterministic lowering from SIR to SBC tables + code bytes.
- Validation for opcode operand widths, table indices, and signature arity/type matches.

## Not Supported
- Unknown section/type/opcode names (rejected by lowering).
- Malformed constants or unresolved/duplicate labels (rejected by lowering).
- Unsupported metadata combinations (rejected by lowering).

## Planned
- Publish the explicit supported SIR subset and the unsupported forms list.
- Add regression fixtures for each unsupported-but-diagnosed SIR class.

## What SIR Means
SIR is the typed intermediate representation between language AST and SBC bytecode.

Purpose:
- isolate language-front-end complexity from SBC emission
- keep lowering rules explicit and testable
- provide a stable textual intermediate for tooling/tests

## SIR Data Model
SIR is section-oriented.

Typical structure:
1. optional metadata sections (`types`, `sigs`, `consts`, `imports`, `globals`)
2. one or more `func` blocks
3. `entry` declaration

Function metadata includes:
- `locals=<u16>`
- `stack=<u16>`
- `sig=<name-or-id>`

## Example: SIR Shape

```txt
types:
  t0 = i32

sigs:
  sig0 = i32(i32,i32)

func add locals=2 stack=2 sig=sig0
  enter 2
  load_local 0
  load_local 1
  add_i32
  ret

entry add
```

## Lowering Rules
`IR/src/ir_lang.cpp` + `IR/src/ir_builder.cpp` enforce:
- names resolved before emission
- opcode operand widths valid for target opcode
- table indices emitted in-range
- generated SBC must satisfy loader/verifier constraints

## Validation Behavior
SIR lowering rejects:
- unknown section/type/opcode names
- malformed constants
- unresolved labels or duplicate labels
- mismatched signatures at calls
- unsupported metadata combinations

## Example: Control Flow Labels

```txt
func loop locals=1 stack=2 sig=sig_main
  enter 1
L0:
  load_local 0
  const_i32 10
  cmp_lt_i32
  jmp_false L1
  load_local 0
  const_i32 1
  add_i32
  store_local 0
  jmp L0
L1:
  ret
```

## Relationship To Language
`Simple::Lang` emits SIR.
`Simple::IR` does not define surface syntax for `.simple`; it defines lowering correctness from SIR to SBC.

## Ownership
- Parser/lowering: `IR/src/ir_lang.cpp`
- Builder/fixups: `IR/src/ir_builder.cpp`
- Headers: `IR/include/ir_lang.h`, `IR/include/ir_builder.h`
