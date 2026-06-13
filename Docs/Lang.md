# Simple::Lang (Current Contract)

This document describes the language front-end as implemented by:

- Lexer: `Lang/src/lang_lexer.cpp`
- Parser: `Lang/src/lang_parser.cpp`
- Validator: `Lang/src/lang_validate.cpp`
- SIR emitter: `Lang/src/lang_sir.cpp`

The language is strict, statically typed, and validates unsupported constructs before
SIR emission.

## Implemented

### Source Pipeline

`.simple` source is processed as:

```txt
source text -> tokens -> AST -> semantic validation -> SIR text
```

Public entry points:

- `ParseProgramFromString`
- `ValidateProgramFromString`
- `EmitSirFromString`
- `EmitSir`

### Program Entry

- Top-level declarations are declarations only.
- Top-level statements are collected into an implicit script entry function.
- If no top-level script statements exist and `main` exists, `main` is used as the entry.
- Top-level `return` is rejected.

Supported `main` style:

```simple
main : i32 () {
  return 0
}
```

### Lexical Features

Implemented token categories include:

- identifiers, integer literals, float literals, strings, chars
- line comments: `// ...`
- block comments: `/* ... */`
- arithmetic operators: `+ - * / %`
- compound assignment: `+= -= *= /= %= &= |= ^= <<= >>=`
- bitwise operators: `& | ^ << >>`
- logical operators: `&& || !`
- comparison operators: `== != < <= > >=`
- mutation operators: `++ --`
- member operators: `.`, `->`
- chain operator: `|>`
- cast introducer: `@`
- function type keyword: `fn`
- control/declaration keywords used by the parser

### Mutability

`:` declares mutable storage. `::` declares immutable storage.

```simple
x : i32 = 1
answer :: i32 = 42
```

Mutability is validated for:

- locals
- globals
- parameters
- artifact fields
- module variables
- assignment targets
- pointer/member mutation paths where represented by the AST/type system

### Primitive Types

Implemented primitive names include:

- signed integers: `i8 i16 i32 i64`
- unsigned integers: `u8 u16 u32 u64`
- floats: `f32 f64`
- `bool`
- `char`
- `string`
- `void` for procedure returns

Runtime/codegen support is strongest for the VM scalar lanes represented by SBC:
`i8/i16/i32/i64`, `u8/u16/u32/u64`, `f32/f64`, `bool`, `char`, `string`, and references.

### Composite Types

Implemented type forms:

```simple
T*       // pointer type syntax
T[]      // dynamic list
T{}      // unsized static array shape
T{N}     // sized static array
fn R (...) // procedure type
```

Examples:

```simple
values : i32[] = [1, 2, 3]
fixed : i32{3} = {1, 2, 3}
ptr : i32* = &value
cb : fn i32 (x : i32)
```

### Procedure Declarations

Implemented procedure declaration syntax:

```simple
name (:|::) ReturnType (params...) { statements }
```

Example:

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}
```

Generic parameter lists are parsed and used by validation/type substitution for supported cases.

### Procedure Values / Function Literals

The AST and validator support procedure types and function literals. Supported forms include assigning a function literal to a `fn`-typed variable.

```simple
sum : fn i32 (a : i32, b : i32) = (a, b) {
  return a + b
}
```

### Declarations

Implemented top-level declarations:

- imports
- extern declarations
- functions
- variables/globals
- artifacts
- modules
- enums

### Modules, Imports, and Using

Implemented module/import handling includes:

- file module headers with `module Name` or dotted names like `module Game.Player`
- internal standard modules through `Lang/include/lang_reserved.h`:
  - `DL`, `Math`, `IO`, `File`, `Buffer`, `Http`, `Socket`, `Time`, `OS`, `Log`
- local/project `.simple` file imports through the CLI loader
- module-name lookup by scanning file headers
- optional `simple.modules` map entries of the form `Name="path/to/file.simple"`
- relative and absolute file imports for compatibility with file-based loading
- ambiguity diagnostics
- cycle detection

`import Name` loads a module and creates a qualified namespace. `using Name` depends on a prior
`import`; a bare `using Name` without a prior import is rejected. Internal modules support
unqualified calls through `using` (for example `sqrt(...)` after `import Math; using Math`).

Examples:

```simple
module App.Main

import DL
import IO
import Math
using Math

main : i32 () {
  IO.println("hello")
  x : f64 = sqrt(9.0) // from `using Math`
  return 0
}
```

`simple.modules` example:

```text
Raylib="raylib/raylib.simple"
Game.Player="src/player.simple"
```

### Extern Declarations and DL Metadata

Implemented extern syntax:

```simple
extern module.symbol: ReturnType (params...)
```

Extern declarations feed metadata used by `System.dl`/`System.dl` dynamic calls.

Implemented ABI validation accepts:

- scalar numeric/boolean/char types
- strings where supported by runtime marshalling
- pointers
- enums
- non-recursive artifacts by value
- nested artifact flattening at the ABI boundary

Recursive artifact structs in extern ABI are rejected; use pointers for recursive structures.

### Artifacts

Implemented artifact declarations:

```simple
Point :: Artifact {
  x : i32
  y : i32
}
```

Implemented artifact features:

- fields
- methods
- `self` inside methods
- positional literals
- named literals
- field defaults where validated/emitted
- method call style through member calls
- artifact layout for VM objects and extern ABI metadata

Examples:

```simple
Point :: Artifact { x : i32 y : i32 }
p : Point = {1, 2}
q : Point = {.x = 1, .y = 2}
```

### Modules

Implemented module declarations:

```simple
Config :: Module {
  Max :: i32 = 10
}

x : i32 = Config.Max
```

Implemented features:

- module variables
- module functions
- defaults for module variables where supported
- member lookup and unknown-member diagnostics

### Enums

Implemented scoped enums:

```simple
Mode :: Enum { Off = 0, On = 1 }
mode : Mode = Mode.On
```

Enum values are strongly scoped; unqualified enum members are rejected.

### Statements and Control Flow

Implemented statements:

- expression statement
- variable declaration
- assignment and compound assignment
- `return`
- `if` / `else if` / `else`
- chain form with `|>` and `default`
- `while`
- C-style `for`
- `break`
- `skip`

Parenthesized conditions are the current supported style:

```simple
if (x > 0) { x -= 1 }
while (x > 0) { x -= 1 }
for (i; i < 10; i += 1) { x += i }
```

For-loop shorthand:

```simple
for (i; i < 10; i += 1) { }
```

is treated as an `i32` loop variable initialized to `0`.

### Switch Expressions

The parser and validator include switch-expression support:

```simple
value : i32 = switch (x) {
  x < 0 => return -1
  x == 0 => return 0
  default => return 1
}
```

Switch branches are validated for value consistency when used in value contexts.

### Expressions

Implemented expression categories include:

- identifiers
- literals
- format strings
- unary expressions
- binary expressions
- assignment expressions
- calls
- type-argument calls where supported
- member access
- pointer member access token `->`
- index access
- list literals
- array literals
- artifact literals
- function literals
- switch expressions

### Casts

Implemented cast syntax:

```simple
x : i32 = @i32(3.14)
y : string = @string(123)
```

Legacy constructor-style primitive casts are rejected.

### Strings and Format Expressions

Implemented string literals, escape handling, and format expression validation.

```simple
name : string = "Sam"
line : string = "name={} score={}", name, score
```

Format placeholders must match argument count. Values must be supported printable scalar/string values.

### Lists and Arrays

Implemented list behavior includes:

- list literals `[]`
- indexing
- `len()`
- `push`
- `pop`
- `insert`
- `remove`
- `clear`

Implemented array behavior includes:

- array literals `{...}` in array contexts
- static size checking for `T{N}`
- indexing
- length support through runtime array operations where emitted

### Diagnostics

Implemented diagnostics include:

- lexer/parser errors with line/column
- semantic errors with line/column where available
- unknown identifier/type/member
- type mismatch
- invalid assignment to immutable storage
- invalid call target/argument count/argument type
- invalid list/array/artifact literal shape
- missing return in non-void functions
- invalid `break`/`skip` outside loops
- invalid top-level `return`
- invalid extern/DL ABI shapes

The CLI wraps many diagnostics with source context and caret display.

## In Progress

These features exist partially, are parsed but not fully guaranteed across all compiler/runtime paths, or are still being hardened:

- full generic language support and broad monomorphization coverage
- first-class procedure values across every runtime edge case
- complete pointer semantics beyond syntax/type validation and supported ABI cases
- switch expression coverage across all combinations of branch/block forms
- exhaustive documentation/tests for every supported SIR emission pattern
- exact alpha compatibility guarantees for generated SIR/SBC
- richer, stable diagnostic error codes beyond the current generic CLI code

## Future

Not currently implemented as a stable contract:

- package manager
- complete optimizing compiler pipeline
- AOT native backend
- mature/full-surface JIT backend
- advanced borrow/lifetime system
- advanced generic constraints/traits/interfaces
- macro system
- async/concurrency language features
- advanced GC tuning exposed to users
- stable cross-version SBC compatibility policy

## Grammar Summary

The implemented grammar is recursive-descent rather than generated. This summary reflects the supported surface at a high level:

```ebnf
program      = { decl | stmt } ;
decl         = import | extern | artifact | module | enum | function | variable ;
variable     = ident (":" | "::") type [ "=" expr ] ;
function     = ident [ generics ] (":" | "::") type "(" [params] ")" block ;
artifact     = ident [ generics ] "::" "Artifact" "{" { field | method } "}" ;
module       = ident "::" "Module" "{" { field | method } "}" ;
enum         = ident "::" "Enum" "{" { ident ["=" integer] [","] } "}" ;
stmt         = return | if | while | for | break | skip | variable | assignment | expr ;
type         = proc_type | base_type {"*"} {"[]" | "{" [integer] "}"} ;
proc_type    = "fn" [generics] type "(" [params] ")" ;
expr         = assignment-expression with the implemented precedence table ;
```
