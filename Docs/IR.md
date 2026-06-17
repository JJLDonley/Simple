# Simple::IR / SIR Contract

`Simple::IR` owns SIR parsing/lowering and SBC binary emission. It is the compiler layer between the language front-end and the bytecode loader/runtime.

Primary implementation files:

- SIR text parser/lowerer: `IR/src/ir_lang.cpp`
- Bytecode builder/fixups: `IR/src/ir_builder.cpp`
- SBC compiler/packer: `IR/src/ir_compiler.cpp`
- Public headers: `IR/include/ir_lang.h`, `IR/include/ir_builder.h`, `IR/include/ir_compiler.h`

## Pipeline

```txt
Simple::Lang AST
  -> SIR text
  -> Simple::IR::Text parser
  -> Simple::IR::IrModule
  -> SBC bytes
  -> Byte loader/verifier
  -> VM execution
```

## Public API

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

## SIR text structure

SIR is section-oriented. Implemented sections are:

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
  type Point kind=object size=16
  field x i32 offset=0
  field y i32 offset=4

sigs:
  sig sig0: (i32, i32) -> i32

consts:
  const c0 string "hello"

globals:
  global g0 i32 init=0

func add locals=2 stack=2 sig=sig0
  enter 2
  load.local 0
  load.local 1
  add i32
  ret
end

entry add
```

## Status and typed syntax

Status values:

- `✅`: accepted by the current SIR parser/lowerer and emitted to assigned SBC bytecode.
- `◐`: typed SIR instruction family is partially implemented; assigned forms are listed in the `Code | T | Syntax` tables.
- `☐`: planned robust typed IR syntax, not accepted by the current lowerer yet.

`Code` is the emitted SBC opcode byte when assigned. `typed` means the row is a collapsed typed family whose assigned concrete opcode bytes are listed below that row.

SIR is intended to stay generic and typed. It should use forms such as `array.get i64` or `list.push f32`; the IR/SBC compiler chooses the concrete SBC opcode (`ArrayGetI64`, `ListPushF32`, etc.). Concrete bytecode suffix mnemonics are compatibility/parser details, not the preferred IR surface.

`<T>` means a typed instruction family over the relevant scalar/reference payload set instead of listing every scalar spelling inline. For numeric scalar families, `<T>` means the valid subset of `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64`; boolean, char, ref, pointer, string, enum, and vector families state their own payload rules.

## Formal SIR grammar

SIR is a line-oriented textual IR. Whitespace separates tokens except inside quoted strings. `;` and `#` start comments outside strings.

```ebnf
module        = { section | function | entry | blank | comment } ;
section       = types | sigs | consts | imports | globals ;
types         = "types:" { type-row | field-row } ;
sigs          = "sigs:" { sig-row } ;
consts        = "consts:" { const-row } ;
imports       = "imports:" { import-row | intrinsic-row | syscall-row } ;
globals       = "globals:" { global-row } ;
function      = func-header { label | instruction } [ "end" ] ;
entry         = "entry" symbol ;
label         = label-name ":" ;
instruction   = mnemonic { operand } ;
comment       = (";" | "#") { any-char } ;
blank         = { whitespace } ;
```

## Section syntax

| Status | Section | Syntax | Meaning | SBC target |
|:---:|---|---|---|---|
| ✅ | types | `type <name> kind=<kind> size=<bytes>` | Defines a type row. | `TypeRow` |
| ✅ | types | `field <name> <type> offset=<bytes>` | Adds a field to the preceding type. | `FieldRow` |
| ✅ | sigs | `sig <name>: (<param>, ...) -> <ret>` | Defines a callable signature. | `SigRow` plus param type list |
| ✅ | consts | `const <name> <kind> <value>` | Defines a constant-pool value. | const pool |
| ✅ | imports | `import <name> <module> <symbol> sig=<sig> [flags=<u32>]` | Defines an external import. | `ImportRow` plus method row |
| ✅ | imports | `intrinsic <name> [=] <id>` | Defines an intrinsic id alias. | import metadata / intrinsic id |
| ✅ | imports | `syscall <name> [=] <id>` | Defines a syscall id alias. | import metadata / syscall id |
| ✅ | globals | `global <name> <type> [init=<const>]` | Defines a typed global. | `GlobalRow` |
| ✅ | function | `func <name> locals=<u16> stack=<u32> [sig=<sig>]` | Starts a function body. | `MethodRow`, `FunctionRow`, code |
| ✅ | function | `local <name> <type> <slot>` | Declares a named/typed local slot. | function-local metadata |
| ✅ | function | `upvalue <name> <type> <slot>` | Declares a named/typed upvalue slot. | function-upvalue metadata |
| ✅ | function | `<label>:` | Defines a branch target. | source-only fixup |
| ✅ | entry | `entry <function>` | Selects module entry method. | `SbcHeader.entry_method_id` |
| ✅ | module | `sir version <major>.<minor>` | Explicit SIR version directive; current supported version is `1.0`. | SBC version/metadata |
| ✅ | module | `module <name>` | Module identity. | module metadata |
| ✅ | exports | `export <symbol> <func> [flags=<u32>]` | Defines an exported function symbol. | `ExportRow` |
| ✅ | debug | `file`, `line`, `symbol` rows | Source-map/debug rows. | debug section |

## Type syntax and SBC type codes

Primitive SIR names lower to SBC `TypeKind` values. Compound forms are planned typed-IR surface unless listed as implemented by the current lowerer.

| Status | SIR type syntax | SBC kind/code | Size | Notes |
|:---:|---|---:|---:|---|
| ✅ | `i32` | `I32` / `1` | 4 | signed integer |
| ✅ | `i64` | `I64` / `2` | 8 | signed integer |
| ✅ | `f32` | `F32` / `3` | 4 | IEEE-754 binary32 |
| ✅ | `f64` | `F64` / `4` | 8 | IEEE-754 binary64 |
| ✅ | `ref` | `Ref` / `5` | word | heap reference |
| ✅ | `i8` | `I8` / `6` | 1 | signed integer |
| ✅ | `i16` | `I16` / `7` | 2 | signed integer |
| ✅ | `u8` | `U8` / `9` | 1 | unsigned integer |
| ✅ | `u16` | `U16` / `10` | 2 | unsigned integer |
| ✅ | `u32` | `U32` / `11` | 4 | unsigned integer |
| ✅ | `u64` | `U64` / `12` | 8 | unsigned integer |
| ✅ | `bool` | `Bool` / `14` | 1 | boolean |
| ✅ | `char` | `Char` / `15` | 2 | UTF/code-unit scalar in current bytecode |
| ✅ | `string` | `String` / `16` | ref | string reference |
| ✅ | object type name | `Ref` or object row kind | declared | resolved by `types:` rows |
| ✅ | `void` | `Void` / `17` | 0 | no-result signature spelling / metadata type |
| ✅ | `never` | `Never` / `18` | 0 | non-returning metadata type |
| ✅ | `ptr<T>` | `Ptr` / `19` | word | typed pointer metadata via `kind=ptr` |
| ✅ | `array<T>` | `Array` / `20` | ref | aggregate metadata via `kind=array` |
| ✅ | `list<T>` | `List` / `21` | ref | aggregate metadata via `kind=list` |
| ✅ | `fn<sig>` | `Function` / `22` | ref | typed function/closure ref metadata via `kind=function` |
| ✅ | `result<T,E>` | `Result` / `23` | ref/value | result metadata via `kind=result` |
| ✅ | `option<T>` | `Option` / `24` | ref/value | optional metadata via `kind=option` |
| ✅ | `vec<T,N>` | `Vector` / `25` | vector | SIMD/vector metadata via `kind=vector` |

## Operand and literal grammar

| Status | Operand | Syntax | Accepted forms | Notes |
|:---:|---|---|---|---|
| ✅ | unsigned integer | `<uN>` | decimal or `0x` via C++ integer parser | bounds checked per opcode |
| ✅ | signed integer | `<iN>` | decimal or `0x`, optional `-` | bounds checked per opcode |
| ✅ | float | `<fN>` | C++ floating parser syntax | used by `const f32`, `const f64` |
| ✅ | boolean | `<bool>` | `0`, `1`, `true`, `false` where accepted | current const lowering accepts numeric bool |
| ✅ | string | `"..."` or `'...'` | supports `\n`, `\r`, `\t`, `\xNN`, quotes, slash | const section only |
| ✅ | symbol | `[A-Za-z_][A-Za-z0-9_]*` | names for labels, sigs, funcs, locals, globals | parser validates labels/signature names |
| ✅ | slot | `<u32>` or `<name>` | index or previously named local/global/upvalue | name resolution is section/function scoped |
| ✅ | label | `<symbol>` | function-local label | lowered to relative branch fixup |
| ✅ | type ref | `<u32>` or `<name>` | numeric type id or type name | checked during lowering |
| ✅ | sig ref | `<u32>` or `<name>` | numeric sig id or sig name | checked during lowering |
| ✅ | const ref | `<u32>` or `<name>` | numeric const offset/id or const name | checked during lowering |
| ✅ | typed immediate | `<T>:<value>` | Explicit literal typing for `const <T>:<value>`. | avoids opcode suffix ambiguity |
| ✅ | source span | `<file> <line>:<col> <line>:<col>` | Accepted by `span` pseudo-instruction and lowered to `Line`. | debug/source mapping |

## Instruction aliases

| Canonical syntax | Alias | Emits |
|---|---|---|
| `load.local <slot>` | `ldloc <slot>` | `LoadLocal` |
| `store.local <slot>` | `stloc <slot>` | `StoreLocal` |
| `load.global <slot>` | `ldglob <slot>` | `LoadGlobal` |
| `store.global <slot>` | `stglob <slot>` | `StoreGlobal` |
| `load.upvalue <slot>` | `ldupv <slot>` | `LoadUpvalue` |
| `store.upvalue <slot>` | `stupv <slot>` | `StoreUpvalue` |
| `load.field <field>` | `ldfld <field>` | `LoadField` |
| `store.field <field>` | `stfld <field>` | `StoreField` |

## Constant-pool contract

| Status | Const kind | SIR syntax | SBC encoding | Notes |
|:---:|---|---|---|---|
| ✅ | string | `const <name> string "..."` | kind + payload offset + byte length + bytes | decoded by `ReadConstPoolString` |
| ✅ | numeric | `const <name> <scalar> <value>` | scalar payload where supported by lowerer | also available as immediate const instructions |
| ✅ | bytes | `const <name> bytes "..."` | length-delimited raw bytes const-pool row (`kind=7`) | accepts string escapes |
| ✅ | data | `const <name> data hex:<blob>` | length-delimited typed data blob const-pool row (`kind=8`) | accepts hex with `_`, `,`, and whitespace separators |
| ✅ | array literal | `const <name> array<i32> [...]` | typed aggregate const row (`kind=6`, count + i32 payload) | reusable const-pool blob |

## Import ABI contract

| Status | Form | Required metadata | Lowering rule |
|:---:|---|---|---|
| ✅ | `import` | module, symbol, signature, flags | creates import row and callable method metadata |
| ✅ | `intrinsic` | name/id | callable through `intrinsic <id>` instruction |
| ✅ | `syscall` | name/id | callable through `syscall <id>` instruction |
| ✅ | `call.import` | import ref, argc, signature | explicit import-call instruction |
| ✅ | `call.native` | native ref, argc, signature | explicit native-call instruction |

All import/native calls must be signature checked by lowering and again by bytecode verification where metadata is available.

## Instruction effects and verifier columns

The instruction table below lists syntax and emitted opcode. The robust typed IR contract additionally requires every instruction family to define:

| Field | Meaning |
|---|---|
| Inputs | typed stack values consumed by the instruction |
| Outputs | typed stack values produced by the instruction |
| Traps | runtime traps possible after verification |
| Verifier rule | static checks required before emission/execution |

These columns are planned for a later expansion of each instruction-family table; until then, stack effects remain synchronized with `Docs/Byte.md` and bytecode opcode metadata.

## Structured IRB model

Textual SIR is the stable inspection/lowering format. `IRB` is the structured language IR that should eventually own typed lowering before SIR serialization.

| Status | IRB node family | Required fields | Lowers to |
|:---:|---|---|---|
| ✅ | module | module identity, imports, globals, funcs | SIR sections |
| ✅ | function | name, signature, locals, blocks/ops | `func` body |
| ✅ | local/global/import/signature allocation | typed symbol metadata | SIR metadata rows |
| ✅ | expression op | typed operator, operands, result type | typed SIR op |
| ✅ | call op | callee, signature, args | `call*` |
| ✅ | aggregate op | array/list/object operation, payload type | aggregate SIR op |
| ✅ | control op | labels/branches/return | branch/return SIR op |
| ☐ | SSA value | id, type, defining op | future middle IR |
| ☐ | basic block | params, ops, terminator | future typed CFG IR |
| ☐ | phi/block args | predecessor values | future SSA lowering |
| ☐ | pass metadata | dominance/liveness/debug | future optimizer input |

## Versioning and diagnostics

| Status | Area | Contract |
|:---:|---|---|
| ✅ | current parser | unknown sections/opcodes/types are hard errors |
| ✅ | current lowering | duplicate labels, unresolved labels, invalid references are hard errors |
| ✅ | SIR version | `sir version <major>.<minor>` directive validates supported SIR text version |
| ☐ | compatibility | planned explicit syntax/version compatibility policy |
| ☐ | diagnostic codes | planned stable `E5xxx` IR/lowering diagnostic code table |
| ☐ | fixtures | planned archived SIR compatibility fixtures |

## Full instruction syntax and codes

### Control and frame

Control-flow, frame, and labels. Labels are source-only and lower to relative branch offsets.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x00` | `nop` | `none` | `Nop` |  |
| ✅ | `0x74` | `enter <locals>` | `u16 locals` | `Enter` |  |
| ✅ | `0x75` | `leave` | `none` | `Leave` |  |
| ✅ | `0x73` | `ret` | `none` | `Ret` |  |
| ✅ | `0x04` | `jmp <label>` | `label` | `Jmp` |  |
| ✅ | `0x05` | `jmp.true <label>` | `label` | `JmpTrue` |  |
| ✅ | `0x06` | `jmp.false <label>` | `label` | `JmpFalse` |  |
| ✅ | `0x07` | `jmptable <default> <case>...` | `default label plus case labels` | `JmpTable` |  |
| ✅ | `0x01` | `halt` | `none` | `Halt` |  |
| ✅ | `0x02` | `trap` | `none` | `Trap` |  |
| ✅ | `0x03` | `breakpoint` | `none` | `Breakpoint` |  |

### Stack

Operand-stack manipulation.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x10` | `pop` | `none` | `Pop` |  |
| ✅ | `0x11` | `dup` | `none` | `Dup` |  |
| ✅ | `0x12` | `dup2` | `none` | `Dup2` |  |
| ✅ | `0x13` | `swap` | `none` | `Swap` |  |
| ✅ | `0x14` | `rot` | `none` | `Rot` |  |

### Constants and data

Immediate constants, constant-pool references, and planned typed data/blob constants.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `const.<T> <value>` | `typed literal` | `Const<T>` | implemented for listed scalar/string/null forms |
| ✅ | `ext 58` | `const.bytes <const>` | `const id/name` | `ConstI32` + `Ext.ConstBytes` | loads bytes const as byte-list ref |
| ✅ | `ext 59` | `const.data <const>` | `const id/name` | `ConstI32` + `Ext.ConstData` | loads data const as byte-list ref |
| ✅ | `ext 60` | `load.dataref <const>` | `const id/name` | `ConstI32` + `Ext.LoadDataRef` | loads data const as byte-list ref |

const.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x18` | `i8` | `const i8 <value>` |
| `0x19` | `i16` | `const i16 <value>` |
| `0x1A` | `i32` | `const i32 <value>` |
| `0x1B` | `i64` | `const i64 <value>` |
| `0x1D` | `u8` | `const u8 <value>` |
| `0x1E` | `u16` | `const u16 <value>` |
| `0x1F` | `u32` | `const u32 <value>` |
| `0x20` | `u64` | `const u64 <value>` |
| `0x22` | `f32` | `const f32 <value>` |
| `0x23` | `f64` | `const f64 <value>` |
| `0x24` | `bool` | `const bool <0|1>` |
| `0x25` | `char` | `const char <u16>` |
| `0x26` | `string` | `const string <const>` |
| `0x27` | `null` | `const null` |

### Locals, globals, upvalues, and module init

Slot access and module/global initialization.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x30` | `ldloc <slot> / load.local <slot>` | `local index/name` | `LoadLocal` |  |
| ✅ | `0x31` | `stloc <slot> / store.local <slot>` | `local index/name` | `StoreLocal` |  |
| ✅ | `0x32` | `ldglob <slot> / load.global <slot>` | `global index/name` | `LoadGlobal` |  |
| ✅ | `0x33` | `stglob <slot> / store.global <slot>` | `global index/name` | `StoreGlobal` |  |
| ✅ | `0x34` | `ldupv <slot> / load.upvalue <slot>` | `upvalue index/name` | `LoadUpvalue` |  |
| ✅ | `0x35` | `stupv <slot> / store.upvalue <slot>` | `upvalue index/name` | `StoreUpvalue` |  |
| ✅ | `0x3D` | `init.global <global>` | `global index/name` | `InitGlobal` | validates initialized global slot |
| ✅ | `0x3E` | `init.module <module>` | `module id` | `InitModule` | module init marker |
| ✅ | `0x3F` | `ensure.module.init <module>` | `module id` | `EnsureModuleInit` | module init guard marker |

### Arithmetic

Binary arithmetic. `<T>` covers every valid numeric scalar type.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `add.<T>` | `none` | `Add<T>` | partially implemented typed family |
| ◐ | `typed` | `sub.<T>` | `none` | `Sub<T>` | partially implemented typed family |
| ◐ | `typed` | `mul.<T>` | `none` | `Mul<T>` | partially implemented typed family |
| ◐ | `typed` | `div.<T>` | `none` | `Div<T>` | partially implemented typed family |
| ◐ | `typed` | `mod.<T>` | `none` | `Mod<T>` | partially implemented typed family |

add.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x40` | `i32` | `add i32` |
| `0x45` | `i64` | `add i64` |
| `0x4A` | `f32` | `add f32` |
| `0x4E` | `f64` | `add f64` |
| `0xE1` | `u32` | `add u32` |
| `0xE6` | `u64` | `add u64` |

sub.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x41` | `i32` | `sub i32` |
| `0x46` | `i64` | `sub i64` |
| `0x4B` | `f32` | `sub f32` |
| `0x4F` | `f64` | `sub f64` |
| `0xE2` | `u32` | `sub u32` |
| `0xE7` | `u64` | `sub u64` |

mul.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x42` | `i32` | `mul i32` |
| `0x47` | `i64` | `mul i64` |
| `0x4C` | `f32` | `mul f32` |
| `0x5C` | `f64` | `mul f64` |
| `0xE3` | `u32` | `mul u32` |
| `0xE8` | `u64` | `mul u64` |

div.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x43` | `i32` | `div i32` |
| `0x48` | `i64` | `div i64` |
| `0x4D` | `f32` | `div f32` |
| `0x5D` | `f64` | `div f64` |
| `0xE4` | `u32` | `div u32` |
| `0xE9` | `u64` | `div u64` |

mod.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x44` | `i32` | `mod i32` |
| `0x49` | `i64` | `mod i64` |
| `0xE5` | `u32` | `mod u32` |
| `0xEA` | `u64` | `mod u64` |

### Increment and decrement

Unary increment/decrement.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `inc.<T>` | `none` | `Inc<T>` | partially implemented typed family |
| ◐ | `typed` | `dec.<T>` | `none` | `Dec<T>` | partially implemented typed family |

inc.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x92` | `i8` | `inc i8` |
| `0x94` | `i16` | `inc i16` |
| `0x83` | `i32` | `inc i32` |
| `0x85` | `i64` | `inc i64` |
| `0x96` | `u8` | `inc u8` |
| `0x98` | `u16` | `inc u16` |
| `0x8B` | `u32` | `inc u32` |
| `0x8D` | `u64` | `inc u64` |
| `0x87` | `f32` | `inc f32` |
| `0x89` | `f64` | `inc f64` |

dec.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x93` | `i8` | `dec i8` |
| `0x95` | `i16` | `dec i16` |
| `0x84` | `i32` | `dec i32` |
| `0x86` | `i64` | `dec i64` |
| `0x97` | `u8` | `dec u8` |
| `0x99` | `u16` | `dec u16` |
| `0x8C` | `u32` | `dec u32` |
| `0x8E` | `u64` | `dec u64` |
| `0x88` | `f32` | `dec f32` |
| `0x8A` | `f64` | `dec f64` |

### Negation

Unary numeric negation.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `neg.<T>` | `none` | `Neg<T>` | partially implemented typed family |

neg.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x9A` | `i8` | `neg i8` |
| `0x9B` | `i16` | `neg i16` |
| `0x5E` | `i32` | `neg i32` |
| `0x5F` | `i64` | `neg i64` |
| `0x9C` | `u8` | `neg u8` |
| `0x9D` | `u16` | `neg u16` |
| `0x9E` | `u32` | `neg u32` |
| `0x9F` | `u64` | `neg u64` |
| `0x7E` | `f32` | `neg f32` |
| `0x7F` | `f64` | `neg f64` |

### Comparisons

Equality and ordering comparisons over comparable typed values.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `cmp.eq.<T>` | `none` | `CmpEq<T>` | partially implemented typed family |
| ◐ | `typed` | `cmp.ne.<T>` | `none` | `CmpNe<T>` | partially implemented typed family |
| ◐ | `typed` | `cmp.lt.<T>` | `none` | `CmpLt<T>` | partially implemented typed family |
| ◐ | `typed` | `cmp.le.<T>` | `none` | `CmpLe<T>` | partially implemented typed family |
| ◐ | `typed` | `cmp.gt.<T>` | `none` | `CmpGt<T>` | partially implemented typed family |
| ◐ | `typed` | `cmp.ge.<T>` | `none` | `CmpGe<T>` | partially implemented typed family |

cmp.eq.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x50` | `i32` | `cmp.eq i32` |
| `0x56` | `i64` | `cmp.eq i64` |
| `0xEB` | `u32` | `cmp.eq u32` |
| `0xF1` | `u64` | `cmp.eq u64` |
| `0x63` | `f32` | `cmp.eq f32` |
| `0x69` | `f64` | `cmp.eq f64` |

cmp.ne.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x52` | `i32` | `cmp.ne i32` |
| `0x57` | `i64` | `cmp.ne i64` |
| `0xEC` | `u32` | `cmp.ne u32` |
| `0xF2` | `u64` | `cmp.ne u64` |
| `0x64` | `f32` | `cmp.ne f32` |
| `0x6A` | `f64` | `cmp.ne f64` |

cmp.lt.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x51` | `i32` | `cmp.lt i32` |
| `0x58` | `i64` | `cmp.lt i64` |
| `0xED` | `u32` | `cmp.lt u32` |
| `0xF3` | `u64` | `cmp.lt u64` |
| `0x65` | `f32` | `cmp.lt f32` |
| `0x6B` | `f64` | `cmp.lt f64` |

cmp.le.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x53` | `i32` | `cmp.le i32` |
| `0x59` | `i64` | `cmp.le i64` |
| `0xEE` | `u32` | `cmp.le u32` |
| `0xF4` | `u64` | `cmp.le u64` |
| `0x66` | `f32` | `cmp.le f32` |
| `0x6C` | `f64` | `cmp.le f64` |

cmp.gt.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x54` | `i32` | `cmp.gt i32` |
| `0x5A` | `i64` | `cmp.gt i64` |
| `0xEF` | `u32` | `cmp.gt u32` |
| `0xF5` | `u64` | `cmp.gt u64` |
| `0x67` | `f32` | `cmp.gt f32` |
| `0x6D` | `f64` | `cmp.gt f64` |

cmp.ge.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x55` | `i32` | `cmp.ge i32` |
| `0x5B` | `i64` | `cmp.ge i64` |
| `0xF0` | `u32` | `cmp.ge u32` |
| `0xF6` | `u64` | `cmp.ge u64` |
| `0x68` | `f32` | `cmp.ge f32` |
| `0x6E` | `f64` | `cmp.ge f64` |

### Boolean logic

Boolean logical operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x60` | `bool.not` | `none` | `BoolNot` |  |
| ✅ | `0x61` | `bool.and` | `none` | `BoolAnd` |  |
| ✅ | `0x62` | `bool.or` | `none` | `BoolOr` |  |

### Bitwise and shifts

Bitwise and shift operations over integer scalar types.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `and.<T>` | `none` | `And<T>` | partially implemented typed family |
| ◐ | `typed` | `or.<T>` | `none` | `Or<T>` | partially implemented typed family |
| ◐ | `typed` | `xor.<T>` | `none` | `Xor<T>` | partially implemented typed family |
| ◐ | `typed` | `shl.<T>` | `none` | `Shl<T>` | partially implemented typed family |
| ◐ | `typed` | `shr.<T>` | `none` | `Shr<T>` | partially implemented typed family |

and.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0xF7` | `i32` | `and i32` |
| `0xD4` | `i64` | `and i64` |

or.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0xF8` | `i32` | `or i32` |
| `0xD5` | `i64` | `or i64` |

xor.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0xF9` | `i32` | `xor i32` |
| `0xD6` | `i64` | `xor i64` |

shl.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0xFA` | `i32` | `shl i32` |
| `0xD7` | `i64` | `shl i64` |

shr.<T> codes:

| Code | T | Syntax |
|---:|---|---|
| `0xFB` | `i32` | `shr i32` |
| `0xD8` | `i64` | `shr i64` |

### Calls

Direct, indirect, tail, import/native, method, and virtual calls.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x70` | `call <func> <argc>` | `function id/name, argc` | `Call` |  |
| ✅ | `0x71` | `call.indirect <sig> <argc>` | `signature id/name, argc` | `CallIndirect` |  |
| ✅ | `0x72` | `tailcall <func> <argc>` | `function id/name, argc` | `TailCall` |  |
| ✅ | `0xE0` | `callcheck` | `none` | `CallCheck` |  |
| ✅ | `0xFE` | `call.import <import> <argc>` | `import id/name, argc` | `CallImport` | metadata-native import call |
| ✅ | `0xFF` | `call.native <native> <argc>` | `native id/name, argc` | `CallNative` | metadata-native import call |
| ✅ | `ext 69` | `call.method <method> <argc>` | `method id/name, argc` | `ConstI32` x2 + `Ext.CallMethod` | direct method call |
| ✅ | `ext 70` | `call.virtual <sig> <argc>` | `signature id/name, argc` | `ConstI32` x2 + `Ext.CallVirtual` | virtual/closure call |

### Conversions

Scalar conversions.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `conv.<From>.<To>` | `none` | `Conv<From,To>` | partially implemented typed family |
| ✅ | `ext 47` | `checked.conv.i32.i64` | `none` | `Ext.CheckedConvI32ToI64` | checked scalar conversion |
| ✅ | `ext 48` | `checked.conv.i64.i32` | `none` | `Ext.CheckedConvI64ToI32` | traps on out-of-range |
| ✅ | `ext 49` | `checked.conv.i32.f32` | `none` | `Ext.CheckedConvI32ToF32` | checked scalar conversion |
| ✅ | `ext 50` | `checked.conv.i32.f64` | `none` | `Ext.CheckedConvI32ToF64` | checked scalar conversion |
| ✅ | `ext 51` | `checked.conv.f32.i32` | `none` | `Ext.CheckedConvF32ToI32` | traps on NaN/infinity/out-of-range |
| ✅ | `ext 52` | `checked.conv.f64.i32` | `none` | `Ext.CheckedConvF64ToI32` | traps on NaN/infinity/out-of-range |
| ✅ | `ext 53` | `checked.conv.f32.f64` | `none` | `Ext.CheckedConvF32ToF64` | checked scalar conversion |
| ✅ | `ext 54` | `checked.conv.f64.f32` | `none` | `Ext.CheckedConvF64ToF32` | traps on finite out-of-range |

conv.<From>.<To> codes:

| Code | T | Syntax |
|---:|---|---|
| `0x76` | `i32 -> i64` | `conv i32 i64` |
| `0x77` | `i64 -> i32` | `conv i64 i32` |
| `0x78` | `i32 -> f32` | `conv i32 f32` |
| `0x79` | `i32 -> f64` | `conv i32 f64` |
| `0x7A` | `f32 -> i32` | `conv f32 i32` |
| `0x7B` | `f64 -> i32` | `conv f64 i32` |
| `0x7C` | `f32 -> f64` | `conv f32 f64` |
| `0x7D` | `f64 -> f32` | `conv f64 f32` |

### Debug, profiling, native, and system runtime

Line/profile markers and runtime/native escape hatches.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x80` | `line <line> <column>` | `u32 line, u32 column` | `Line` |  |
| ✅ | `0x81` | `profile.start <id>` | `u32 id` | `ProfileStart` |  |
| ✅ | `0x82` | `profile.end <id>` | `u32 id` | `ProfileEnd` |  |
| ✅ | `0x90` | `intrinsic <id>` | `u32 id` | `Intrinsic` |  |
| ✅ | `0x91` | `syscall <id>` | `u32 id` | `SysCall` |  |
| ✅ | pseudo | `span <file> <start> <end>` | `source span` | `Line` | lowers to start-line marker |
| ✅ | `0x29` | `trace.enter <id>` | `u32 id` | `TraceEnter` |  |
| ✅ | `0x2A` | `trace.leave <id>` | `u32 id` | `TraceLeave` |  |
| ✅ | `0x28` | `stacktrace` | `none` | `StackTrace` |  |

### Objects, closures, refs, and fields

Heap object, closure, reference, field, lifecycle, and typed reference operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0xA0` | `newobj <type>` | `type id/name` | `NewObject` |  |
| ✅ | `0xA1` | `newclosure <method> <upvalues>` | `method id/name, upvalue count` | `NewClosure` |  |
| ✅ | `0xA2` | `ldfld <field>` | `field id/name` | `LoadField` |  |
| ✅ | `0xA3` | `stfld <field>` | `field id/name` | `StoreField` |  |
| ✅ | `0xA4` | `isnull` | `none` | `IsNull` |  |
| ✅ | `0xA5` | `ref.eq` | `none` | `RefEq` |  |
| ✅ | `0xA6` | `ref.ne` | `none` | `RefNe` |  |
| ✅ | `0xA7` | `typeof` | `none` | `TypeOf` |  |
| ✅ | `ext 63` | `capture.local <slot>` | `local index/name` | `ConstI32` + `Ext.CaptureLocal` | pushes local for closure capture |
| ✅ | `ext 64` | `capture.ref <slot>` | `local index/name` | `ConstI32` + `Ext.CaptureRef` | pushes local ref for closure capture |
| ✅ | `ext 65` | `close.upvalue <slot>` | `upvalue index/name` | `ConstI32` + `Ext.CloseUpvalue` | validates and closes captured upvalue |
| ✅ | `ext 55` | `init.object <type>` | `type id/name` | `ConstI32` + `Ext.InitObject` | allocate and initialize object |
| ✅ | `0x2D` | `drop.object` | `none` | `DropObject` |  |
| ✅ | `0x2E` | `clone.object` | `none` | `CloneObject` |  |
| ✅ | `0x2F` | `object.eq` | `none` | `ObjectEq` | structural payload equality |
| ✅ | `ext 43` | `instanceof[.<T>] <type?>` | `type id/name` | `ConstI32` + `Ext.InstanceOf` | runtime type test |
| ✅ | `ext 44` | `cast.ref[.<T>] <type?>` | `type id/name` | `ConstI32` + `Ext.CastRef` | unchecked reference cast |
| ✅ | `ext 45` | `checked.cast.ref[.<T>] <type?>` | `type id/name` | `ConstI32` + `Ext.CheckedCastRef` | checked reference cast |
| ✅ | `ext 46` | `load.vtable` | `none` | `Ext.LoadVTable` | load object type/vtable id |

### Arrays

Fixed-size array allocation and element access.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `newarray <T> <length>` | `type, length` | `NewArray<T>` | generic SIR; compiler selects concrete SBC opcode |
| ✅ | `0xB1` | `array.len` | `none` | `ArrayLen` |  |
| ◐ | `typed` | `array.get <T>` | `type` | `ArrayGet<T>` | generic SIR; compiler selects concrete SBC opcode |
| ◐ | `typed` | `array.set <T>` | `type` | `ArraySet<T>` | generic SIR; compiler selects concrete SBC opcode |
| ✅ | `0x17` | `array.copy` | `none` | `ArrayCopy<T>` | pops src, src index, dst, dst index, count |
| ✅ | `0x8F` | `array.fill` | `none` | `ArrayFill<T>` | pops array, i32 count, fill value |

newarray <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0xB0` | `generic` | `newarray i32 <length>` |
| `0xB4` | `i64` | `newarray i64 <length>` |
| `0xB7` | `f32` | `newarray f32 <length>` |
| `0xBA` | `f64` | `newarray f64 <length>` |
| `0xBD` | `ref` | `newarray ref <type> <length>` |

array.get <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0xB2` | `i32` | `array.get i32` |
| `0xB5` | `i64` | `array.get i64` |
| `0xB8` | `f32` | `array.get f32` |
| `0xBB` | `f64` | `array.get f64` |
| `0xBE` | `ref` | `array.get ref` |

array.set <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0xB3` | `i32` | `array.set i32` |
| `0xB6` | `i64` | `array.set i64` |
| `0xB9` | `f32` | `array.set f32` |
| `0xBC` | `f64` | `array.set f64` |
| `0xBF` | `ref` | `array.set ref` |

### Lists

Growable list allocation and element operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ◐ | `typed` | `newlist <T> <capacity>` | `type, capacity` | `NewList<T>` | generic SIR; compiler selects concrete SBC opcode |
| ✅ | `0xC1` | `list.len` | `none` | `ListLen` |  |
| ◐ | `typed` | `list.get <T>` | `type` | `ListGet<T>` | generic SIR; compiler selects concrete SBC opcode |
| ◐ | `typed` | `list.set <T>` | `type` | `ListSet<T>` | generic SIR; compiler selects concrete SBC opcode |
| ◐ | `typed` | `list.push <T>` | `type` | `ListPush<T>` | generic SIR; compiler selects concrete SBC opcode |
| ◐ | `typed` | `list.pop <T>` | `type` | `ListPop<T>` | generic SIR; compiler selects concrete SBC opcode |
| ◐ | `typed` | `list.insert <T>` | `type` | `ListInsert<T>` | generic SIR; compiler selects concrete SBC opcode |
| ◐ | `typed` | `list.remove <T>` | `type` | `ListRemove<T>` | generic SIR; compiler selects concrete SBC opcode |
| ✅ | `0xC8` | `list.clear` | `none` | `ListClear` |  |
| ✅ | `0xAF` | `list.reserve` | `none` | `ListReserve` | pops list and i32 capacity |
| ✅ | `0x6F` | `list.resize` | `none` | `ListResize` | pops list, i32 size, fill value |

newlist <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x36` | `ref` | `newlist ref` |
| `0xA8` | `f64` | `newlist f64` |
| `0xC0` | `i32` | `newlist i32 <capacity>` |
| `0xC9` | `f32` | `newlist f32` |
| `0xD9` | `i64` | `newlist i64` |

list.get <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x37` | `ref` | `list.get ref` |
| `0xA9` | `f64` | `list.get f64` |
| `0xC2` | `i32` | `list.get i32` |
| `0xCA` | `f32` | `list.get f32` |
| `0xDA` | `i64` | `list.get i64` |

list.set <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x38` | `ref` | `list.set ref` |
| `0xAA` | `f64` | `list.set f64` |
| `0xC3` | `i32` | `list.set i32` |
| `0xCB` | `f32` | `list.set f32` |
| `0xDB` | `i64` | `list.set i64` |

list.push <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x39` | `ref` | `list.push ref` |
| `0xAB` | `f64` | `list.push f64` |
| `0xC4` | `i32` | `list.push i32` |
| `0xCC` | `f32` | `list.push f32` |
| `0xDC` | `i64` | `list.push i64` |

list.pop <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x3A` | `ref` | `list.pop ref` |
| `0xAC` | `f64` | `list.pop f64` |
| `0xC5` | `i32` | `list.pop i32` |
| `0xCD` | `f32` | `list.pop f32` |
| `0xDD` | `i64` | `list.pop i64` |

list.insert <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x3B` | `ref` | `list.insert ref` |
| `0xAD` | `f64` | `list.insert f64` |
| `0xC6` | `i32` | `list.insert i32` |
| `0xCE` | `f32` | `list.insert f32` |
| `0xDE` | `i64` | `list.insert i64` |

list.remove <T> SBC selection:

| Code | T | Syntax |
|---:|---|---|
| `0x3C` | `ref` | `list.remove ref` |
| `0xAE` | `f64` | `list.remove f64` |
| `0xC7` | `i32` | `list.remove i32` |
| `0xCF` | `f32` | `list.remove f32` |
| `0xDF` | `i64` | `list.remove i64` |

### Strings

String-specific operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0xD0` | `string.len` | `none` | `StringLen` |  |
| ✅ | `0xD1` | `string.concat` | `none` | `StringConcat` |  |
| ✅ | `0xD2` | `string.get.char` | `none` | `StringGetChar` |  |
| ✅ | `0xD3` | `string.slice` | `none` | `StringSlice` |  |
| ✅ | `0xFC` | `string.eq` | `none` | `StringEq` |  |
| ✅ | `0xFD` | `string.ne` | `none` | `StringNe` |  |
| ✅ | `0x15` | `string.compare` | `none` | `StringCompare` |  |
| ✅ | `0x16` | `string.find` | `none` | `StringFind` |  |
| ✅ | `ext 56` | `string.to.bytes` | `none` | `Ext.StringToBytes` | converts string ref to byte-list ref |
| ✅ | `ext 57` | `bytes.to.string` | `none` | `Ext.BytesToString` | converts byte-list ref to string ref |

### Pointer and memory

Explicit pointer/address and raw memory operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | pseudo | `addrof.local <slot>` | `local` | `LoadLocal` | address marker alias |
| ✅ | pseudo | `addrof.global <slot>` | `global` | `LoadGlobal` | address marker alias |
| ✅ | pseudo | `addrof.field <field>` | `field` | `LoadField` | address marker alias |
| ✅ | pseudo | `load.ptr.<T>` | `none` | no-op marker | preserves pointer/value marker |
| ✅ | pseudo | `store.ptr.<T>` | `none` | `Pop` + `Pop` | consumes pointer/value markers |
| ✅ | pseudo | `ptr.add` | `none` | `AddI32` | pointer offset marker over integer offsets |
| ✅ | pseudo | `ptr.offset` | `none` | `AddI32` | pointer offset marker over integer offsets |
| ✅ | pseudo | `ptr.eq` | `none` | `RefEq` | pointer/reference equality alias |
| ✅ | pseudo | `ptr.ne` | `none` | `RefNe` | pointer/reference inequality alias |
| ✅ | pseudo | `ptr.isnull` | `none` | `IsNull` | null-test alias |
| ✅ | pseudo | `ptr.check.null` | `none` | `CheckedNull` | null-check alias |
| ✅ | pseudo | `ptr.check.bounds` | `none` | `CheckedBounds` | bounds-check alias |
| ✅ | pseudo | `mem.copy` | `none` | `Pop` x3 | consumes dst/src/len markers |
| ✅ | pseudo | `mem.move` | `none` | `Pop` x3 | consumes dst/src/len markers |
| ✅ | pseudo | `mem.set` | `none` | `Pop` x3 | consumes dst/value/len markers |
| ✅ | pseudo | `mem.compare` | `none` | `ConstI32 0` | consumes lhs/rhs/len markers and pushes equality placeholder |

### Checked operations

Checked arithmetic, bounds, null, and conversions.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `ext 1` | `checked.add.i32` | `none` | `Ext.CheckedAddI32` | traps on signed i32 overflow |
| ✅ | `ext 6` | `checked.add.i64` | `none` | `Ext.CheckedAddI64` | traps on signed i64 overflow |
| ✅ | `ext 11` | `checked.add.u32` | `none` | `Ext.CheckedAddU32` | traps on unsigned u32 overflow |
| ✅ | `ext 16` | `checked.add.u64` | `none` | `Ext.CheckedAddU64` | traps on unsigned u64 overflow |
| ✅ | `ext 2` | `checked.sub.i32` | `none` | `Ext.CheckedSubI32` | traps on signed i32 overflow |
| ✅ | `ext 7` | `checked.sub.i64` | `none` | `Ext.CheckedSubI64` | traps on signed i64 overflow |
| ✅ | `ext 12` | `checked.sub.u32` | `none` | `Ext.CheckedSubU32` | traps on unsigned u32 overflow |
| ✅ | `ext 17` | `checked.sub.u64` | `none` | `Ext.CheckedSubU64` | traps on unsigned u64 overflow |
| ✅ | `ext 3` | `checked.mul.i32` | `none` | `Ext.CheckedMulI32` | traps on signed i32 overflow |
| ✅ | `ext 8` | `checked.mul.i64` | `none` | `Ext.CheckedMulI64` | traps on signed i64 overflow |
| ✅ | `ext 13` | `checked.mul.u32` | `none` | `Ext.CheckedMulU32` | traps on unsigned u32 overflow |
| ✅ | `ext 18` | `checked.mul.u64` | `none` | `Ext.CheckedMulU64` | traps on unsigned u64 overflow |
| ✅ | `ext 4` | `checked.div.i32` | `none` | `Ext.CheckedDivI32` | traps on divide-by-zero and signed i32 overflow |
| ✅ | `ext 9` | `checked.div.i64` | `none` | `Ext.CheckedDivI64` | traps on divide-by-zero and signed i64 overflow |
| ✅ | `ext 14` | `checked.div.u32` | `none` | `Ext.CheckedDivU32` | traps on divide-by-zero |
| ✅ | `ext 19` | `checked.div.u64` | `none` | `Ext.CheckedDivU64` | traps on divide-by-zero |
| ✅ | `ext 5` | `checked.mod.i32` | `none` | `Ext.CheckedModI32` | traps on divide-by-zero and signed i32 overflow |
| ✅ | `ext 10` | `checked.mod.i64` | `none` | `Ext.CheckedModI64` | traps on divide-by-zero and signed i64 overflow |
| ✅ | `ext 15` | `checked.mod.u32` | `none` | `Ext.CheckedModU32` | traps on divide-by-zero |
| ✅ | `ext 20` | `checked.mod.u64` | `none` | `Ext.CheckedModU64` | traps on divide-by-zero |
| ✅ | `ext 21` | `checked.array.get.i32` | `none` | `Ext.CheckedArrayGetI32` | null/type/bounds checked array load |
| ✅ | `ext 22` | `checked.array.set.i32` | `none` | `Ext.CheckedArraySetI32` | null/type/bounds checked array store |
| ✅ | `ext 23` | `checked.array.get.i64` | `none` | `Ext.CheckedArrayGetI64` | null/type/bounds checked array load |
| ✅ | `ext 24` | `checked.array.set.i64` | `none` | `Ext.CheckedArraySetI64` | null/type/bounds checked array store |
| ✅ | `ext 25` | `checked.array.get.f32` | `none` | `Ext.CheckedArrayGetF32` | null/type/bounds checked array load |
| ✅ | `ext 26` | `checked.array.set.f32` | `none` | `Ext.CheckedArraySetF32` | null/type/bounds checked array store |
| ✅ | `ext 27` | `checked.array.get.f64` | `none` | `Ext.CheckedArrayGetF64` | null/type/bounds checked array load |
| ✅ | `ext 28` | `checked.array.set.f64` | `none` | `Ext.CheckedArraySetF64` | null/type/bounds checked array store |
| ✅ | `ext 29` | `checked.array.get.ref` | `none` | `Ext.CheckedArrayGetRef` | null/type/bounds checked array load |
| ✅ | `ext 30` | `checked.array.set.ref` | `none` | `Ext.CheckedArraySetRef` | null/type/bounds checked array store |
| ✅ | `ext 31` | `checked.list.get.i32` | `none` | `Ext.CheckedListGetI32` | null/type/bounds checked list load |
| ✅ | `ext 32` | `checked.list.set.i32` | `none` | `Ext.CheckedListSetI32` | null/type/bounds checked list store |
| ✅ | `ext 33` | `checked.list.get.i64` | `none` | `Ext.CheckedListGetI64` | null/type/bounds checked list load |
| ✅ | `ext 34` | `checked.list.set.i64` | `none` | `Ext.CheckedListSetI64` | null/type/bounds checked list store |
| ✅ | `ext 35` | `checked.list.get.f32` | `none` | `Ext.CheckedListGetF32` | null/type/bounds checked list load |
| ✅ | `ext 36` | `checked.list.set.f32` | `none` | `Ext.CheckedListSetF32` | null/type/bounds checked list store |
| ✅ | `ext 37` | `checked.list.get.f64` | `none` | `Ext.CheckedListGetF64` | null/type/bounds checked list load |
| ✅ | `ext 38` | `checked.list.set.f64` | `none` | `Ext.CheckedListSetF64` | null/type/bounds checked list store |
| ✅ | `ext 39` | `checked.list.get.ref` | `none` | `Ext.CheckedListGetRef` | null/type/bounds checked list load |
| ✅ | `ext 40` | `checked.list.set.ref` | `none` | `Ext.CheckedListSetRef` | null/type/bounds checked list store |
| ✅ | `ext 41` | `checked.string.get.char` | `none` | `Ext.CheckedStringGetChar` | null/type/bounds checked string char load |
| ✅ | `ext 42` | `checked.string.slice` | `none` | `Ext.CheckedStringSlice` | null/type/bounds checked string slice |
| ✅ | `0x2B` | `checked.null` | `none` | `CheckedNull` |  |
| ✅ | `0x2C` | `checked.bounds` | `none` | `CheckedBounds` | pops value, index, length; pushes value |

### Enums, variants, results, and errors

Tagged data and error operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | pseudo | `enum.tag` | `none` | `ConstI32 0` | consumes enum marker and pushes tag placeholder |
| ✅ | pseudo | `enum.payload.<T> <case>` | `case` | no-op marker | preserves payload marker |
| ✅ | pseudo | `enum.make.<T> <case>` | `case` | no-op marker | preserves payload marker |
| ✅ | pseudo | `variant.tag` | `none` | `ConstI32 0` | consumes variant marker and pushes tag placeholder |
| ✅ | pseudo | `variant.payload.<T> <case>` | `case` | no-op marker | preserves payload marker |
| ✅ | pseudo | `variant.make.<T> <case>` | `case` | no-op marker | preserves payload marker |
| ✅ | pseudo | `result.ok.<T>` | `none` | no-op marker | preserves ok value marker |
| ✅ | pseudo | `result.err.<T>` | `none` | no-op marker | preserves err value marker |
| ✅ | pseudo | `result.is.ok` | `none` | `ConstBool true` | consumes result marker and pushes status placeholder |
| ✅ | pseudo | `result.is.err` | `none` | `ConstBool true` | consumes result marker and pushes status placeholder |
| ✅ | pseudo | `result.unwrap.<T>` | `none` | no-op marker | preserves value marker |
| ✅ | pseudo | `result.propagate.err` | `none` | no-op marker | preserves result marker |
| ✅ | `ext 61` | `throw` | `none` | `Ext.Throw` | raises current exception/traps in current VM |
| ✅ | pseudo | `catch <label>` | `label` | no-op marker | validates handler label |
| ✅ | pseudo | `finally <label>` | `label` | no-op marker | validates cleanup label |
| ✅ | `ext 62` | `panic` | `none` | `Ext.Panic` | unconditional panic trap |

### Range and iterators

Typed range/iterator bytecodes.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | pseudo | `range.new.<T>` | `none` | marker value | consumes end marker and preserves start marker |
| ✅ | pseudo | `range.new.step.<T>` | `none` | marker value | consumes step/end markers and preserves start marker |
| ✅ | pseudo | `range.next.<T>` | `none` | marker value + false | iterator/range next placeholder |
| ✅ | pseudo | `iter.next.<T>` | `none` | marker value + false | iterator next placeholder |
| ✅ | pseudo | `iter.has.next` | `none` | `ConstBool false` | iterator has-next placeholder |
| ✅ | pseudo | `iter.value.<T>` | `none` | no-op marker | preserves iterator value marker |

### Concurrency, atomics, locks, and monitors

Thread/job/channel/atomic/monitor operations.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | pseudo | `spawn <func>` | `function` | `ConstI32` | task marker returns function id handle |
| ✅ | pseudo | `join` | `none` | no-op marker | preserves handle/result marker |
| ✅ | pseudo | `detach` | `none` | `Pop` | consumes task marker handle |
| ✅ | pseudo | `await` | `none` | no-op marker | preserves awaited marker value |
| ✅ | `0x0E` | `yield` | `none` | `Yield` | scheduler yield marker |
| ✅ | pseudo | `resume` | `none` | `Pop` | consumes continuation marker |
| ✅ | pseudo | `suspend` | `none` | `Yield` + `ConstI32` | yields and returns placeholder continuation |
| ✅ | pseudo | `future.make <func>` | `function` | `ConstI32` | future marker returns function id handle |
| ✅ | pseudo | `future.poll` | `none` | no-op marker | preserves future marker value |
| ✅ | pseudo | `channel.send.<T>` | `none` | `Pop` + `Pop` | consumes channel/value markers |
| ✅ | pseudo | `channel.recv.<T>` | `none` | placeholder value | consumes channel marker and pushes placeholder value |
| ✅ | pseudo | `channel.try.recv.<T>` | `none` | placeholder status/value | consumes channel marker and pushes `false`, placeholder value |
| ✅ | pseudo | `atomic.load.<T>` | `none` | no-op marker | preserves marker value |
| ✅ | pseudo | `atomic.store.<T>` | `none` | `Pop` + `Pop` | consumes address/value markers |
| ✅ | pseudo | `atomic.add.<T>` | `none` | `Add<T>` | atomic arithmetic marker for integer scalars |
| ✅ | pseudo | `atomic.sub.<T>` | `none` | `Sub<T>` | atomic arithmetic marker for integer scalars |
| ✅ | pseudo | `atomic.cmpxchg.<T>` | `none` | placeholder result | consumes address/expected/value markers and pushes `0` |
| ✅ | `0x0F` | `fence` | `none` | `Fence` | sequentially consistent VM fence |
| ✅ | pseudo | `lock` | `none` | `Pop` | consumes monitor marker |
| ✅ | pseudo | `unlock` | `none` | `Pop` | consumes monitor marker |
| ✅ | pseudo | `trylock` | `none` | `Pop` + `ConstBool true` | consumes monitor marker and pushes success placeholder |
| ✅ | pseudo | `wait` | `none` | `Pop` | consumes monitor marker |
| ✅ | pseudo | `notify` | `none` | `Pop` | consumes monitor marker |
| ✅ | pseudo | `notify.all` | `none` | `Pop` | consumes monitor marker |

### GC, JIT, capabilities, and SIMD

Runtime coordination, optimizing backend hooks, sandbox checks, and vectors.

| Status | Code | Syntax | Operands | Emits | Notes |
|:---:|---:|---|---|---|---|
| ✅ | `0x08` | `safepoint` | `none` | `Safepoint` |  |
| ✅ | `0x09` | `alloc.checkpoint` | `none` | `AllocCheckpoint` |  |
| ✅ | `ext 71` | `write.barrier` | `none` | `Ext.WriteBarrier` | records/validates object-to-ref write barrier |
| ✅ | `ext 72` | `read.barrier` | `none` | `Ext.ReadBarrier` | validates and returns loaded ref |
| ✅ | `ext 73` | `pin.ref` | `none` | `Ext.PinRef` | validates and pins live ref for current operation |
| ✅ | `ext 74` | `unpin.ref` | `none` | `Ext.UnpinRef` | validates and unpins ref |
| ✅ | `0x0A` | `keepalive` | `none` | `KeepAlive` |  |
| ✅ | pseudo | `deopt <id>` | `u32 id` | no-op marker | deoptimization marker alias |
| ✅ | pseudo | `patchpoint <id>` | `u32 id` | no-op marker | JIT patchpoint marker alias |
| ✅ | pseudo | `inline.cache <id>` | `u32 id` | no-op marker | inline-cache marker alias |
| ✅ | `ext 68` | `guard.type[.<T>] <type?>` | `type` | `ConstI32` + `Ext.GuardType` | runtime type guard |
| ✅ | `ext 67` | `guard.bounds` | `none` | `Ext.GuardBounds` | checked bounds guard |
| ✅ | `ext 66` | `guard.notnull` | `none` | `Ext.GuardNotNull` | non-null guard |
| ✅ | `0x0B` | `cap.check <id>` | `u32 id` | `CheckCapability` | capability marker |
| ✅ | `0x0C` | `sandbox.enter <id>` | `u32 id` | `EnterSandbox` | sandbox marker |
| ✅ | `0x0D` | `sandbox.exit` | `none` | `ExitSandbox` | sandbox marker |
| ☐ | `TBD` | `vec.load.<T,N>` | `none` | `VecLoad<T,N>` | planned |
| ☐ | `TBD` | `vec.store.<T,N>` | `none` | `VecStore<T,N>` | planned |
| ☐ | `TBD` | `vec.splat.<T,N>` | `none` | `VecSplat<T,N>` | planned |
| ☐ | `TBD` | `vec.extract.<T,N> <lane>` | `lane` | `VecExtract<T,N>` | planned |
| ☐ | `TBD` | `vec.add.<T,N>` | `none` | `VecAdd<T,N>` | planned |
| ☐ | `TBD` | `vec.sub.<T,N>` | `none` | `VecSub<T,N>` | planned |
| ☐ | `TBD` | `vec.mul.<T,N>` | `none` | `VecMul<T,N>` | planned |
| ☐ | `TBD` | `vec.div.<T,N>` | `none` | `VecDiv<T,N>` | planned |
| ☐ | `TBD` | `vec.and.<T,N>` | `none` | `VecAnd<T,N>` | planned |
| ☐ | `TBD` | `vec.or.<T,N>` | `none` | `VecOr<T,N>` | planned |
| ☐ | `TBD` | `vec.xor.<T,N>` | `none` | `VecXor<T,N>` | planned |

## Module model

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

Functions track name, locals count, max stack, signature id/name, local names/types, upvalue names/types, labels, and instruction list.

## Lowering and validation

Implemented lowering handles primitive/object type rows, fields, signatures, constants, globals, imports, function rows, method rows, labels, relative branch fixups, and jump-table fixups.

IR/compiler diagnostics reject unknown sections, malformed entries, malformed signatures/constants, unknown type names, unknown opcode names, invalid operands, duplicate labels, unresolved labels, duplicate or missing entry points, and unsupported metadata combinations known to the lowerer. Additional bytecode validation is performed by the Byte loader/verifier after compilation.

## Current compiler invariants

- Lang must emit SIR accepted by `ParseIrTextModule` and `LowerIrTextToModule`.
- IR lowering must emit SBC accepted by the Byte loader.
- Byte verifier remains the final bytecode safety check before VM execution.
- Label targets must resolve to valid instruction boundaries.
- Signature mismatches must be rejected, not coerced.
- Unknown opcodes/sections/types are errors.
