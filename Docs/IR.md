# Simple::IR / Compiler (Current Contract)

`Simple::IR` owns SIR parsing/lowering and SBC binary emission. It is the compiler layer between the language front-end and the bytecode loader/runtime.

Primary implementation files:

- SIR text parser/lowerer: `IR/src/ir_lang.cpp`
- Bytecode builder/fixups: `IR/src/ir_builder.cpp`
- SBC compiler/packer: `IR/src/ir_compiler.cpp`
- Public headers: `IR/include/ir_lang.h`, `IR/include/ir_builder.h`, `IR/include/ir_compiler.h`

## Implemented

### Compiler Pipeline Position

Implemented compiler path:

```txt
Simple::Lang AST
  -> SIR text
  -> Simple::IR::Text parser
  -> Simple::IR::IrModule
  -> SBC bytes
  -> Byte loader/verifier
```

The language front-end emits SIR text. The IR layer parses and lowers that text into a binary SBC module.

### Public API

Implemented APIs:

```cpp
bool ParseIrTextModule(const std::string& text,
                       IrTextModule* out,
                       std::string* error);

bool LowerIrTextToModule(const IrTextModule& text,
                         Simple::IR::IrModule* out,
                         std::string* error);

bool CompileToSbc(const IrModule& module,
                  std::vector<uint8_t>* out,
                  std::string* error);
```

### SIR Text Structure

Implemented SIR is section-oriented. Supported sections include:

- `types:`
- `sigs:`
- `consts:`
- `imports:`
- `globals:`
- `func ...`
- `entry ...`

Representative shape:

```txt
types:
  Point = object size=16

sigs:
  sig0 = i32(i32,i32)

consts:
  c0 = string "hello"

globals:
  g0 : i32 = 0

func add locals=2 stack=2 sig=sig0
  enter 2
  load_local 0
  load_local 1
  add_i32
  ret

entry add
```

### Text Module Model

Implemented parsed model includes:

- `IrTextType`
- `IrTextField`
- `IrTextSig`
- `IrTextConst`
- `IrTextGlobal`
- `IrTextImport`
- `IrTextFunction`
- `IrTextInst`
- `IrTextModule`

Functions track:

- name
- locals count
- max stack
- signature id/name
- local names/types
- upvalue names/types
- instruction list

### Type and Signature Lowering

Implemented lowering handles:

- primitive type names used by SBC type rows
- object/artifact type rows
- field metadata
- signature rows
- signature parameter type lists
- return type ids
- call/import signature references

### Constants

Implemented const handling includes:

- string constants
- numeric constants represented in SIR syntax and lowered where supported
- const-pool offsets used by SBC rows and constant opcodes

The const pool is emitted as SBC bytes and revalidated by the Byte loader.

### Globals

Implemented global handling includes:

- global rows
- global type ids
- optional initializer constants
- global load/store op emission through SIR instructions

### Imports

Implemented import lowering supports runtime/native/import metadata rows with:

- module name
- symbol name
- signature reference
- flags

Language extern and reserved runtime imports are emitted into this section by `Lang/src/lang_sir.cpp`.

### Functions and Labels

Implemented function lowering includes:

- opcode parsing
- operand parsing
- local/upvalue name resolution
- labels
- duplicate-label rejection
- unresolved-label rejection
- relative branch fixups through `IrBuilder`
- jump table fixups
- function code offsets/sizes
- method rows
- function rows

Example:

```txt
func loop locals=1 stack=2 sig=sig0
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

### Opcode Emission

Implemented `IrBuilder` methods cover the current SBC opcode surface, including:

- control flow
- stack ops
- constants
- calls
- locals/globals/upvalues
- arithmetic and comparison lanes
- boolean ops
- conversions
- objects/fields/closures
- arrays
- lists
- strings
- intrinsics/syscalls
- debug/profile opcodes

`IrBuilder::Finish` resolves labels/fixups and produces final code bytes.

### SBC Compilation

Implemented `CompileToSbc` packs an `IrModule` into SBC bytes with:

- header
- section table
- aligned sections
- types/fields/methods/sigs/globals/functions/imports/exports
- const pool
- code
- debug bytes where present

SBC structural validity is ultimately checked by `Byte/src/sbc_loader.cpp`.

### Validation and Diagnostics

Implemented IR/compiler diagnostics reject:

- unknown sections
- malformed section entries
- malformed signatures
- malformed constants
- unknown type names
- unknown opcode names
- invalid operand widths/values
- invalid table references caught during lowering
- duplicate labels
- unresolved labels
- duplicate or missing entry where required
- unsupported metadata combinations known to the lowerer

Additional bytecode-level validation is performed by the Byte loader/verifier after compilation.

## In Progress

These areas are implemented enough for current language/test usage but are not yet frozen as a stable external IR contract:

- complete formal SIR grammar
- exhaustive list of every accepted opcode mnemonic and operand form
- stable textual SIR compatibility/versioning policy
- full structured IR builder replacing string-oriented SIR emission from Lang
- richer typed section builders instead of raw metadata byte buffers in `IrModule`
- complete debug section contract
- complete export section language-facing contract
- archived SIR compatibility fixtures
- stable diagnostic codes for SIR parse/lower errors

## Future

Not currently implemented as stable compiler contract:

- optimizing middle-end passes
- SSA-based IR
- register allocation
- AOT/native backend
- stable external plugin API for compiler passes
- cross-module incremental compilation
- package-aware module graph compiler
- formal IR version negotiation
- source-map/debug-info format freeze

## Current Compiler Invariants

- Lang must emit SIR accepted by `ParseIrTextModule` and `LowerIrTextToModule`.
- IR lowering must emit SBC accepted by the Byte loader.
- Byte verifier remains the final bytecode safety check before VM execution.
- Label targets must resolve to valid instruction boundaries.
- Signature mismatches must be rejected, not coerced.
- Unknown opcodes/sections/types are errors.
