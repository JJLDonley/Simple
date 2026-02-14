# Simple::Lang (API)

This document is authoritative for Simple language syntax and semantics.

## Supported
- Strict typing with explicit mutability markers (`:` mutable, `::` immutable).
- Primitive types: `i8 i16 i32 i64 i128`, `u8 u16 u32 u64 u128`, `f32 f64`, `bool char string`.
- Composite/supporting types: arrays `T[N]`, lists `T[]`, pointers `*T` and `*void`.
- Variable declarations with explicit types and initializers.
- Procedure declarations with explicit signatures and `return`.
- First-class procedure values via `fn`.
- `callback` parameter type marker (parameter positions only).
- Control flow: `if/else`, chained guards (`|> ...`), `while`, `for`.
- Enums with scoped member access (`Status.Running`).
- Artifacts with fields, methods, and explicit field mutability.
- Module namespaces with constant members.
- Imports:
  - reserved stdlib imports (see `Docs/StdLib.md`)
  - file/path imports (importer-relative resolution)
- `extern` declarations for native ABI binding through `DL`.
- Script-style entry behavior:
  - top-level statements execute in source order via implicit `__script_entry`
  - top-level function declarations do not execute implicitly
  - explicit `main` is used only when no top-level script statements exist

## Not Supported
- Top-level `return` (rejected at validation).
- Legacy cast syntax `T(value)` (use `@T(value)` instead).
- Import cycles (rejected at validation).
- Using `callback` as a variable/field/return type (parameter-only).

## Planned
- Publish the explicit supported syntax/features list for alpha.
- Publish the explicit deferred/unsupported syntax list for alpha.
- Freeze diagnostic format expectations across parser/validator classes.

## Core Principles
- Strict typing everywhere
- Explicit mutability model
- Deterministic validation errors
- No implicit fallback for unsupported constructs

## Mutability
- `:` mutable binding/member
- `::` immutable binding/member

Examples:

```simple
x : i32 = 10
name :: string = "Simple"
```

## Type System
Primitives:
- `i8 i16 i32 i64 i128`
- `u8 u16 u32 u64 u128`
- `f32 f64`
- `bool char string`

Composite/supporting:
- arrays: `T[N]`
- lists: `T[]`
- pointers: `*T`, `*void`
- user types: `Artifact`, `Enum`, module namespaces (lowercase `artifact`/`enum`/`module` still accepted)

## Lists
- list literals (`[a, b, c]`) allocate a list with capacity equal to the literal length.
- lists grow automatically (like `std::vector`) on `push`/`insert`.
- list methods: `list.len()`, `list.push(value)`, `list.pop()` (or `list.pop(index)`),
  `list.insert(index, value)`, `list.remove(index)`, `list.clear()`.

## Variable Declarations

```simple
count : i32 = 42
label :: string = "ready"
```

## Procedures

### Procedure Declarations

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}
```

### First-Class Procedure Values (`fn`)
Procedure value binding syntax follows signature style:
- mutable: `x : fn = RetType (params...) { ... }`
- immutable: `x :: fn = RetType (params...) { ... }`

Examples:

```simple
f : fn = i32 (a : i32, b : i32) { return a + b }
update : fn = void (p : Player) { p.position.y += p.velocity.x }
result : i32 = f(20, 22)
```

### Callback Parameter Type
`callback` is a dedicated parameter type marker for procedure-accepting params.
- valid in parameter positions
- not a general variable/field/return type

Example:

```simple
run : void (cb : callback) {
  cb()
}
```

## Control Flow

### If/Else/Chains
Chain form:

```simple
|> hp <= 0 { state = 0 }
|> hp < 50 { state = 1 }
|> default { state = 2 }
```

Equivalent `if/else` form:

```simple
if hp <= 0 {
  state = 0
} else {
  if hp < 50 {
    state = 1
  } else {
    state = 2
  }
}
```

### While

```simple
i : i32 = 0
while i < 10 {
  i += 1
}
```

### For

```simple
sum : i32 = 0
for i : i32 = 0; i < 10; i += 1 {
  sum += i
}
```

Range form (inclusive):

```simple
sum : i32 = 0
for i : i32 = 0; 0..10 {
  sum += i
}
```

If the loop variable omits an explicit type, it defaults to `i32` and initializes from the range start:

```simple
sum : i32 = 0
for i; 0..10 {
  sum += @i32(i)
}
```

Explicit header init is allowed; the range start still controls the loop:

```simple
sum : i32 = 0
for i : i32 = 5; 0..10 {
  sum += i
}
```

## Enums (Scoped)

```simple
Status :: Enum { Idle = 0, Running = 1, Failed = 2 }

s : Status = Status.Running
```

Enums are strongly typed and scoped under their enum name.

## Artifacts
Artifacts are structured user-defined types with optional methods.

### Field Mutability
- mutable field: `field : T`
- immutable field: `field :: T`

Example:

```simple
Player :: Artifact {
  hp : i32

  damage : void (amount : i32) {
    self.hp -= amount
  }
}
```

### Initialization Styles

```simple
p1 : Player = { 100 }
p2 : Player = { .hp = 100 }
```

### Method Call Style

```simple
p : Player = { .hp = 100 }
p.damage(10)
```

## Namespaces / Modules With Constants

```simple
Module Config {
  MAX_PLAYERS :: i32 = 16
}

count : i32 = Config.MAX_PLAYERS
```

## Imports
Simple supports two import domains.

### Reserved Library Imports
Examples:

```simple
import System.io
import System.dl as DL
import FS
```

These map to compiler/runtime-reserved modules (see `Docs/StdLib.md`).

### File/Path Imports
Examples:

```simple
import "raylib"
import "./raylib.simple"
import "../another/raylib.simple"
```

For local project imports, resolution is importer-relative.

## Extern + DLL Interop Entry
`extern` declarations define typed signatures used by `DL` dynamic loading.
See `Docs/StdLib.md` and `Docs/VM.md` for ABI/runtime details.

## Diagnostics Contract
- Type mismatches are compile errors.
- Invalid mutability writes are compile errors.
- Unsupported syntax/constructs fail explicitly.

## Source Ownership
- Lexer: `Lang/src/lang_lexer.cpp`
- Parser: `Lang/src/lang_parser.cpp`
- Validator: `Lang/src/lang_validate.cpp`
- SIR emission: `Lang/src/lang_sir.cpp`
