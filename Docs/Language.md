# Simple Language Reference

Simple is a strict, statically typed language that compiles `.simple` source to SIR text and then to SBC bytecode for the Simple VM.

This page is the canonical language reference for the syntax and behavior covered by the current parser, validator, fixtures, and tests.

## Table of contents

- [Quick example](#quick-example)
- [Compilation pipeline](#compilation-pipeline)
- [Lexical rules](#lexical-rules)
- [Program structure and entry points](#program-structure-and-entry-points)
- [File/package headers](#filepackage-headers)
- [Declarations](#declarations)
- [Mutability](#mutability)
- [Types](#types)
- [Literals](#literals)
- [Expressions](#expressions)
- [Casts](#casts)
- [Statements and control flow](#statements-and-control-flow)
- [Arrays and lists](#arrays-and-lists)
- [Strings and formatting](#strings-and-formatting)
- [Artifacts](#artifacts)
- [Modules](#modules)
- [Enums](#enums)
- [Imports and `using`](#imports-and-using)
- [Reserved/System modules and standard library](#reservedsystem-modules-and-standard-library)
- [Functions, procedure types, and function literals](#functions-procedure-types-and-function-literals)
- [Extern declarations and DL ABI](#extern-declarations-and-dl-abi)
- [Pointers and member access](#pointers-and-member-access)
- [Diagnostics](#diagnostics)
- [Known limitations](#known-limitations)
- [Compatibility APIs](#compatibility-apis)

## Quick example

```simple
import IO

Point :: Artifact {
  x : i32
  y : i32

  sum : i32 () {
    return self.x + self.y
  }
}

add : i32 (a : i32, b : i32) {
  return a + b
}

main : i32 () {
  p : Point = { .x = 3, .y = 4 }
  IO.println("sum={}", p.sum())
  return add(p.x, p.y)
}
```

Important syntax facts:

- `name : Type` declares a **mutable** binding.
- `name :: Type` declares an **immutable** binding.
- `package Name` declares the file/package header used by import indexing.
- `Name :: Artifact`, `Name :: Module`, and `Name :: Enum` declare top-level kinds.
- `skip` is the loop-continue statement.
- Primitive casts use `@Type(value)`, for example `@i32(x)`.

## Compilation pipeline

```txt
.simple source
  -> Lexer tokens
  -> CAST parser tree
  -> normalized AST
  -> RAST name/import/member resolution
  -> TAST type, mutability, control-flow, and ABI checks
  -> IRB language IR
  -> IRE SIR text
  -> IR/SBC compiler
  -> Byte loader/verifier
  -> VM
```

The interpreter is the runtime correctness baseline. Language validation rejects unsupported syntax/semantics before SIR emission where possible.

## Lexical rules

### Keywords

Most keywords are lowercase:

```txt
while for break skip return if else default switch fn self
package import using extern as true false
```

The declaration-kind keywords accept the capitalized forms used by existing fixtures:

```txt
artifact Artifact
enum     Enum
module   Module
```

Use `package` for file/package headers. Use `Name :: Module { ... }` for in-language namespace objects.

### Operators and punctuation

Supported punctuation/operators include:

```txt
( ) { } [ ] , . -> => .. ; : :: =
+ - * / % ++ --
& | ^ << >>
== != < <= > >= && || !
+= -= *= /= %= &= |= ^= <<= >>= |>
@
```

Semicolons are accepted as statement terminators. Newlines/block boundaries can also terminate statements where the parser can disambiguate them.

### Comments and whitespace

Whitespace separates tokens. Comments are ignored by the lexer. Same-line multiple statements require semicolons:

```simple
main : i32 () { x : i32 = 1; y : i32 = 2; return x + y; }
```

### Numeric literals

Integer literals support decimal, hex (`0x10`), and binary (`0b1010`) forms where integer parsing is used, including fixed-size array dimensions.

Floating literals are context-typed as `f32` or `f64` by expected type.

### String and character escapes

Strings and chars support normal escapes and hex escapes. Invalid escapes are rejected by lexer tests.

```simple
main : i32 () {
  c : char = '\x41'
  s : string = "A\x42"
  return 0
}
```

## Program structure and entry points

A file may start with an optional package header and may then contain imports, extern declarations, top-level declarations, and top-level script statements.

```simple
package Examples.Math
import Math

square : i32 (x : i32) {
  return x * x
}

main : i32 () {
  return square(7)
}
```

Entry behavior:

- If top-level executable statements exist, they are normalized into an implicit script entry.
- If no top-level executable statements exist and `main` exists, `main` is used as the entry.
- Top-level `return` is invalid.
- A `main : void ()` function is valid; a missing explicit return is valid for `void`.

## File/package headers

A file may start with a package header:

```simple
package Tools.Widget
```

The package header gives the file an import-index name. It does **not** declare a runtime namespace object and it does **not** wrap the declarations that follow it.

```simple
package Tools.Widget

widgetValue : i32 () {
  return 42
}
```

Another file can import that package name:

```simple
import Tools.Widget

main : i32 () {
  return widgetValue()
}
```

Rules:

- The header keyword is `package`.
- The package name is an identifier path, for example `Main`, `Lib`, or `Tools.Widget`.
- The header should appear before imports/declarations/statements.
- Only import indexing uses the package name; ordinary language lookup still uses declarations, imports, modules, and `using`.
- Old `module Name` file headers are intentionally rejected to avoid confusion with `Name :: Module { ... }` declarations.

Use `Name :: Module { ... }` only when you want a language module/namespace value:

```simple
Math :: Module {
  one : i32 () { return 1 }
}
```

## Declarations

### Variables

Mutable variable:

```simple
count : i32 = 0
count += 1
```

Immutable variable:

```simple
limit :: i32 = 10
```

A variable declaration may omit an initializer; it is zero/default-initialized when the type supports that path.

```simple
x : i32
```

### Functions

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}
```

Function declarations use name-first syntax:

```txt
name : ReturnType (params...) { body }
```

The marker before the return type also carries return mutability facts used by validation.

### Top-level declarations with `::`

`::` introduces immutable top-level values and declaration kinds:

```simple
Pi :: f64 = 3.141592
Point :: Artifact { x : i32; y : i32 }
Math :: Module { one : i32 () { return 1 } }
Color :: Enum { Red = 1, Green = 2 }
```

## Mutability

Mutability is part of declarations and parameters:

```simple
main : i32 () {
  x : i32 = 1    // mutable
  y :: i32 = 2   // immutable
  x = x + 1      // ok
  return x + y
}
```

The validator rejects writes to immutable values:

- immutable locals
- immutable parameters
- immutable fields
- immutable module variables
- immutable return values
- fields/indexes reached through an immutable base
- increment/decrement on immutable values
- writes through immutable pointer-like values

Parameters follow the same marker rule:

```simple
use : i32 (x : i32, y :: i32) {
  x = x + 1   // ok
  return x + y
}
```

## Types

### Primitive types

The type parser accepts:

```txt
i8 i16 i32 i64 i128
u8 u16 u32 u64 u128
f32 f64
bool char string void
```

Some parsed types may still be rejected by later compiler/runtime layers if the backend or ABI path does not support them.

### Fixed arrays

Fixed arrays use braces after the element type:

```simple
values : i32{3} = {1, 2, 3}
empty : i32{0} = {}
grid : i32{2}{2} = {{1, 2}, {3, 4}}
hexSized : i32{0x10}
binSized : i32{0b1010}
```

### Lists

Lists use `[]` after the element type and list literals use brackets:

```simple
items : i32[] = [1, 2, 3]
nested : i32[][] = [[1, 2], [3, 4]]
empty : i32[] = []
```

### Generic type syntax

The parser accepts generic type syntax:

```simple
Map<string, i32>
```

Generic emission/runtime support is intentionally limited; tests reject unsupported generic emission and specialization paths.

### Procedure types

```simple
fn i32 ()
fn bool (i32, string)
fn i32 (a : i32, b : i32)
```

Procedure values are supported in local variables, parameters, switch expressions, and artifact members where covered by tests. Procedure values are rejected at extern ABI boundaries and in unsupported generic/list/array contexts.

### Pointer types

```simple
i32*
void**
```

Pointer member access uses `->`. Pointer mutability is validated so immutable pointees cannot be mutated through aliases.

## Literals

Supported literal categories:

```simple
42                 // integer
1.5                // float
true false         // bool
'a' '\x41'         // char
"text" "A\x42"    // string
{1, 2, 3}          // fixed array literal
[1, 2, 3]          // list literal
{ .x = 1, .y = 2 } // named artifact literal
```

Artifact literals may also be positional where the target type makes field order clear:

```simple
p : Point = { 3, 4 }
```

## Expressions

Expression forms include:

- identifiers
- literals
- unary operators
- binary operators
- calls
- member access (`.`)
- pointer member access (`->`)
- indexing (`value[index]`)
- array/list/artifact literals
- function literals
- switch expressions
- format strings

Examples:

```simple
values : i32[] = [4, 5, 6]
third : i32 = values[2]

p : Point = { .x = 3, .y = 4 }
s : i32 = p.sum()
```

Arithmetic and comparison are type-checked. Examples rejected by tests include bool arithmetic, char arithmetic, char-vs-int comparison, non-integer indexes, indexing non-containers, out-of-bounds constant indexes, and mismatched list/array element types.

## Casts

Primitive casts require the `@` prefix:

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}

main : i32 () {
  a : i8 = 40
  b : i8 = 2
  return add(@i32(a), @i32(b))
}
```

String conversions use the same syntax where supported:

```simple
main : string () {
  x : i32 = 42
  return @string(x)
}
```

Using primitive type names as normal functions for casts is rejected. The expected diagnostic contains “primitive cast syntax requires '@'”.

## Statements and control flow

### Return

```simple
main : i32 () {
  return 0
}
```

Non-void functions must return on all required paths. `void` functions may fall through.

### If / else

```simple
main : i32 () {
  x : i32 = 7
  if (x > 5) {
    return 1
  } else {
    return 0
  }
}
```

Nested `if`/`else` chains are normalized and checked for return flow.

### While

```simple
main : i32 () {
  i : i32 = 0
  sum : i32 = 0
  while (i < 10) {
    i += 1
    if (i == 5) { skip }
    if (i == 8) { break }
    sum += i
  }
  return sum
}
```

`break` and `skip` are valid only inside loops.

### For

Three-part `for` loops are supported:

```simple
main : i32 () {
  total : i32 = 0
  for (i : i32 = 1; i <= 5; i += 1) {
    total += i
  }
  return total
}
```

Existing-loop-variable form is also supported:

```simple
i : i32 = 1
for (i; i <= 10; i += 1) {
  // ...
}
```

The step expression may use assignment or increment/decrement:

```simple
for (i : i32 = 0; i < 10; i++) { skip }
for (i : i32 = 0; i < 10; i = i + 1) { skip }
for (i : i32 = 0; i < 10; i += 2) { skip }
```

Malformed ranges/headers are rejected by parser tests.

### Switch expressions

Switch expressions use `=>` branches and `default`:

```simple
main : i32 () {
  x : i32 = 1
  return switch (x) {
    x > 0 => { local : i32 = 10; return local }
    default => return 0
  }
}
```

Switch branch locals are scoped to the branch. Validation checks branch shapes and result compatibility.

## Arrays and lists

Fixed arrays:

```simple
values : i32{3} = {1, 2, 3}
values[1] = 9
return values[1]
```

Lists:

```simple
items : i32[] = []
items.push(10)
items.push(20)
return items.len()
```

The global `len(value)` helper is supported for strings, arrays, and lists where tests cover it.

List methods covered by fixtures include:

```simple
items.len()
items.push(value)
items.pop()
items.insert(index, value)
items.remove(index)
items.clear()
```

## Strings and formatting

Strings are first-class heap values:

```simple
main : i32 () {
  s : string = "hello"
  return len(s)
}
```

IO formatting validates placeholder calls. Examples use standard-library printing:

```simple
import IO

main : void () {
  IO.print("answer={}", 42)
  IO.println(" done")
}
```

Formatting and standard-library calls are part of the standard library surface summarized below.

## Artifacts

Artifacts are record-like declarations with fields and methods:

```simple
Point :: Artifact {
  x : i32
  y : i32

  sum : i32 () {
    return self.x + self.y
  }
}
```

Construction:

```simple
p1 : Point = { 3, 4 }
p2 : Point = { .y = 4, .x = 3 }
```

Inside artifact methods, fields must be accessed through `self` unless a local binding intentionally shadows a name. Tests reject unqualified artifact-member access such as `return x` when `self.x` is required.

Artifact methods may mutate mutable fields:

```simple
Counter :: Artifact {
  value : i32

  inc : void () {
    self.value += 1
  }
}
```

Artifact ABI flattening is used for supported extern/DL cases. Recursive artifact ABI is rejected.

## Modules

Modules group variables and functions under a namespace:

```simple
Math :: Module {
  base :: i32 = 2

  add : i32 (a : i32, b : i32) {
    return a + b
  }
}

main : i32 () {
  return Math.add(Math.base, 3)
}
```

Unknown module members, using modules as types, and assigning to immutable module variables are rejected.

## Enums

Enums use `:: Enum` and require qualified access:

```simple
Color :: Enum {
  Red = 1,
  Green = 2
}

main : i32 () {
  return Color.Green
}
```

Tests reject unqualified enum variants (`Green` instead of `Color.Green`), unknown enum members, and using the enum type itself as a value.

## Imports and `using`

Imports accept quoted paths, unquoted module paths, and aliases:

```simple
import "raylib"
import "raylib" as Ray
import IO
import FS as FileSystem
import System.io
```

`using` imports members into unqualified call scope:

```simple
import Channel
using Channel

main : i32 () {
  ch : i64 = newI32()
  sendI32(ch, 9)
  return recvI32(ch)
}
```

CLI import resolution handles project-root imports, relative imports, module-map entries, reserved imports, missing imports, ambiguous imports, and cycle detection. Generated `simple.modules` files are build artifacts.

## Reserved/System modules and standard library

`System.*` is canonical for standard-library modules. Reserved compatibility imports are mapped by the compiler/runtime. Covered modules include:

```txt
Math IO Time File DL OS FS Log Buffer Json Channel
Env Path Random Thread
System.math System.io System.time System.fs System.dl System.os
System.env System.path System.random System.thread System.channel
System.buffer System.json System.log
```

The standard library is part of the language-facing runtime surface. Reserved imports map onto native-backed runtime modules; no implicit ABI coercion is performed. If a module/member is not listed here or covered by tests, treat it as unsupported.

### Import mapping

| Import | Runtime namespace |
|---|---|
| `Math` / `System.math` | `System.math` |
| `IO` / `System.io` | `System.io` |
| `Time` / `System.time` | `System.time` |
| `File` / `FS` / `System.fs` | `System.fs` |
| `DL` / `System.dl` | `System.dl` |
| `OS` / `System.os` | `System.os` |
| `Env` / `System.env` | `System.env` |
| `Path` / `System.path` | `System.path` |
| `Random` / `System.random` | `System.random` |
| `Thread` / `System.thread` | `System.thread` |
| `Log` / `System.log` | `System.log` |
| `Buffer` / `System.buffer` | `System.buffer` |
| `Json` / `System.json` | `System.json` |
| `Channel` / `System.channel` | `System.channel` |

### Core modules

| Module | Examples / members |
|---|---|
| `Math` | `abs`, `min`, `max`, `sqrt`, `PI` |
| `IO` | `print`, `println`, `buffer_new`, `buffer_len`, `buffer_fill`, `buffer_copy` |
| `Time` | `mono_ns`, `wall_ns`, `formatWallNs` in using-style tests |
| `FS` / `File` | file descriptors, `read`, `write`, `close`, `readBytes`, `writeBytes`, `listDir` |
| `OS` | args, env, cwd, sleep/time/platform constants |
| `Env` | environment helpers covered by reserved env fixtures |
| `Path` | path helpers covered by reserved path fixtures |
| `Random` | random helpers covered by reserved random fixtures |
| `Thread` | thread helpers covered by reserved thread fixtures |
| `Log` | `log`, `info`, `warn`, `error`, `setLevel`, `setFile` |
| `Buffer` | `new`, `len`, `readU16LE`, `readU32LE`, `writeU16LE`, `writeU32LE`, `slice`, `copy` |
| `Json` | `parse`, `stringify`, `free` |
| `Channel` | typed channel creation plus `send*`, `trySend*`, `recv*`, `tryRecv*`, `pending*`, `close` |
| `DL` | `open`, `sym`, `close`, `last_error`, `supported`; also `DL.Open(path, ffi)` fixture style |

`using ModuleName` exposes module members for unqualified calls where that module supports it:

```simple
import Buffer
using Buffer

main : i32 () {
  b : i32[] = new(4)
  return len(b)
}
```

## Functions, procedure types, and function literals

Procedure values use `fn` types:

```simple
main : i32 () {
  f : fn i32 (a : i32, b : i32) = (a, b) { return a + b }
  return f(40, 2)
}
```

Procedure variables and procedure parameters are validated by tests. Function literals can appear in supported typed contexts, including call arguments where the receiving type is known.

Unsupported/rejected procedure cases include:

- closure captures in current procedure literal emission
- nested closure forms that require unsupported capture behavior
- procedure values at extern ABI boundaries
- procedure values inside unsupported list/array/generic emission paths
- direct inline invocation of an anonymous function literal

## Extern declarations and DL ABI

Extern declarations describe imported host or dynamic-library functions:

```simple
extern puts : i32 (s : string)
extern Ray.InitWindow : void (w : i32, h : i32)
extern ffi.simple_add_i32 : i32 (a : i32, b : i32)
```

Extern names may be module-qualified. Calls are checked for argument count and type compatibility.

Dynamic-library usage is exposed through `DL` / `System.dl` runtime APIs. Example shape from fixtures:

```simple
import DL

extern ffi.simple_add_i32 : i32 (a : i32, b : i32)

lib :: i64 = DL.Open("Tests/ffi/libsimpleffi.so", ffi)

main : i32 () {
  return ffi.simple_add_i32(40, 2)
}
```

ABI restrictions are strict. Unsupported ABI types and recursive artifact ABI are rejected.

## Pointers and member access

Pointer types use `*` suffixes:

```simple
i32*
void**
```

Pointer member access uses `->`:

```simple
node->value
```

The validator tracks mutability through pointer-like access and rejects mutation through immutable values.

## Diagnostics

Compiler diagnostics include:

- a stable error code where available
- compiler phase (`RAST`, `TAST`, etc.)
- source line/column
- message
- optional help text

CLI renders diagnostics as terminal output. LSP converts the same diagnostic information into editor diagnostics.

Common rejected cases covered by tests include:

- unknown identifiers/types/members
- duplicate qualified symbols
- type mismatches
- missing returns
- invalid loop control outside loops
- invalid imports
- invalid casts without `@`
- invalid ABI shapes
- immutable assignment
- bad array/list/index usage
- malformed lexer/parser input

## Known limitations

Current tests intentionally reject or limit:

- full generic function/artifact emission
- generic specialization naming/emission paths
- closure capture for procedure literals
- procedure values at extern boundaries
- procedure values in unsupported containers/generic contexts
- recursive artifact ABI for extern/DL
- direct inline invocation of anonymous function literals
- using modules/functions as types or enum types as values

## Compatibility APIs

Embedders can use the compatibility APIs:

```cpp
ParseProgramFromString(source, &program, &error);
ValidateProgramFromString(source, &error);
ValidateProgramFromStringDiagnostic(source, &diagnostic);
EmitSirFromString(source, &sir, &error);
EmitSir(program, &sir, &error);
ParseTypeFromString(type_text, &type, &error);
```

New compiler implementation code should use the phase APIs (`CAST`, `AST`, `RAST`, `TAST`, `IRB`, `IRE`) directly, while public compatibility APIs remain available for current embedders.
