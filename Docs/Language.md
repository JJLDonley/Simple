# Simple Language Reference

Simple: identity-first, strictly typed — scripted in minutes, portable everywhere.

It compiles `.simple` source to SIR text and then to portable SBC bytecode for the Simple VM, with JIT, GC, canonical System and Standard libraries, and explicit FFI.

This page is the canonical language reference for the syntax and behavior covered by the current parser, validator, fixtures, and tests.

## Table of contents

- [Quick example](#quick-example)
- [Compilation pipeline](#compilation-pipeline)
- [Status markers](#status-markers)
- [Formal grammar overview](#formal-grammar-overview)
- [Full language syntax tables](#full-language-syntax-tables)
- [Lexical rules](#lexical-rules)
- [Program structure and entry points](#program-structure-and-entry-points)
- [File/module headers](#filemodule-headers)
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
- [Extern declarations and FFI ABI](#extern-declarations-and-ffi-abi)
- [Pointers and member access](#pointers-and-member-access)
- [Diagnostics](#diagnostics)
- [Known limitations](#known-limitations)
- [Compatibility APIs](#compatibility-apis)

## Quick example

```simple
import Standard.IO

Point :: data {
  x : i32
  y : i32
}

Counter :: artifact {
  value : i32

  inc : i32 () {
    return self.value + 1
  }
}

add : i32 (a : i32, b : i32) {
  return a + b
}

main : i32 () {
  p : Point = { .x = 3, .y = 4 }
  c : Counter = { .value = 6 }
  Standard.IO.println("next={}", c.inc())
  return add(p.x, p.y)
}
```

Important syntax facts:

- `name : Type` declares a **mutable** binding.
- `name :: Type` declares an **immutable** binding.
- `module Name` declares the file/module header used by import indexing.
- `Name :: artifact`, `Name :: data`, `Name :: namespace`, and `Name :: enum` declare top-level kinds.
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

## Status markers

Language syntax tables use the same status style as the IR and Byte references:

| Status | Meaning |
|:---:|---|
| ✅ | Implemented and covered by parser/validator/runtime tests or fixtures. |
| ◐ | Partially implemented; supported in the common path but with documented restrictions. |
| ☐ | Planned or reserved language surface; not accepted as stable syntax yet. |
| ❌ | Intentionally rejected syntax/semantic form. |

`Phase` identifies the compiler stage that owns the rule:

| Phase | Owns |
|---|---|
| Lexer | tokens, comments, literals, keywords |
| CAST | concrete parse shape and syntax errors |
| AST | normalized source tree |
| RAST | names, imports, modules, members |
| TAST | types, mutability, control-flow, ABI rules |
| IRB/IRE | structured lowering and SIR emission |
| Runtime | behavior checked only during VM execution |

## Formal grammar overview

This is the high-level grammar contract for accepted source. Details are refined by the syntax tables below.

```ebnf
program        = [ module-header-decl ] { import-decl | using-decl | extern-decl | top-decl | stmt } ;
module-header-decl   = "module" qualified-name ;
import-decl    = "import" qualified-name [ "as" ident ] ;
using-decl     = "using" qualified-name ;
extern-decl    = "extern" [ qualified-name ] function-signature ;
top-decl       = var-decl | func-decl | artifact-decl | data-decl | namespace-decl | enum-decl ;

var-decl       = ident (":" | "::") type [ "=" expr ] ;
func-decl      = ident ":" type "(" [ params ] ")" block ;
artifact-decl  = ident "::" "artifact" "{" { field-decl | func-decl } "}" ;
data-decl      = ident "::" "data" "{" { field-decl } "}" ;
namespace-decl = ident "::" "namespace" "{" { top-decl } "}" ;
enum-decl      = ident "::" "enum" "{" enum-member { enum-member } "}" ;

stmt           = var-decl | assign-stmt | expr-stmt | if-stmt | switch-stmt | while-stmt | for-stmt |
                 break-stmt | skip-stmt | return-stmt | block ;
block          = "{" { stmt } "}" ;

expr           = literal | ident | member-expr | index-expr | call-expr | cast-expr | unary-expr |
                 binary-expr | assignment-expr | fn-literal | artifact-literal | list-literal |
                 switch-expr ;
type           = primitive-type | named-type | array-type | list-type | proc-type | pointer-type |
                 generic-type ;
```

## Full language syntax tables

### Lexical tokens

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | identifier | Lexer | Names start with a letter or `_`, followed by alnum/underscore. |
| ✅ | integer literal | Lexer/TAST | Decimal, hex `0x`, and binary `0b` forms where integer parsing is used. |
| ✅ | float literal | Lexer/TAST | Context typed as `f32`/`f64`; rejected where no float context exists. |
| ✅ | string literal | Lexer | Double-quoted with escapes. |
| ✅ | char literal | Lexer | Single-quoted char/escape. |
| ✅ | comment | Lexer | Comments are ignored. |
| ✅ | semicolon | CAST | Explicit statement separator. |
| ✅ | newline/block boundary | CAST | Statement separator where unambiguous. |
| ❌ | invalid escape | Lexer | Rejected with lexer diagnostic. |
| ❌ | unknown character | Lexer | Rejected before parsing. |

### Keywords and reserved words

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | `module` | CAST/RAST | File/module header. |
| ✅ | `import` | RAST | Imports another module identity source. |
| ✅ | `using` | RAST | Brings reserved/native module members into lookup. |
| ✅ | `extern` | RAST/TAST | Declares external/native callable. |
| ✅ | `while`, `for`, `break`, `skip`, `return` | CAST/TAST | Loop/control statements. |
| ✅ | `if`, `else`, `switch`, `default` | CAST/TAST | Branching constructs. |
| ✅ | `fn` | CAST/TAST | Procedure type/literal marker. |
| ✅ | `self` | RAST/TAST | Artifact method receiver. |
| ✅ | `true`, `false` | CAST/TAST | Boolean literals. |
| ✅ | `artifact` | CAST/TAST | Artifact declaration kind. |
| ✅ | `namespace` | CAST/RAST | Namespace declaration kind. |
| ✅ | `enum` | CAST/TAST | Enum declaration kind. |
| ❌ | keyword as identifier | CAST | Rejected except where keyword is expected syntax. |

### Operators and punctuation

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | `:` | CAST/TAST | Mutable declaration marker or type separator. |
| ✅ | `::` | CAST/TAST | Immutable declaration marker / top-level kind declaration marker. |
| ✅ | `=` | TAST | Assignment/initializer. |
| ✅ | `+ - * / %` | TAST | Numeric arithmetic. |
| ✅ | `++ --` | TAST/IRE | Inc/dec on supported numeric lvalues. |
| ✅ | `& | ^ << >>` | TAST | Integer bitwise and shifts. |
| ✅ | `== != < <= > >=` | TAST | Equality/order comparisons over supported types. |
| ✅ | `&& || !` | TAST | Boolean logic. |
| ✅ | `+= -= *= /= %= &= |= ^= <<= >>=` | TAST/IRE | Compound assignment. |
| ✅ | `.` | RAST/TAST | Module/member/field access. |
| ✅ | `->` | TAST | Pointer member access where pointer semantics are supported. |
| ✅ | `..` | TAST/IRE | Range form used by `for`. |
| ✅ | `|>` | CAST/TAST | Pipe expression where supported. |
| ✅ | `@` | TAST | Primitive cast syntax. |
| ✅ | `( ) { } [ ] , ;` | CAST | Grouping, blocks, indexing/literals, separators. |
| ❌ | primitive call cast like `i32(x)` | TAST | Rejected; use `@i32(x)`. |

### Declarations

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | `name : Type` | TAST | Mutable binding with default init if no initializer. |
| ✅ | `name : Type = expr` | TAST | Mutable binding with typed initializer. |
| ✅ | `name :: Type = expr` | TAST | Immutable binding; must not be assigned later. |
| ✅ | `name : Ret (params) block` | TAST | Function declaration. |
| ✅ | `Name :: artifact { ... }` | TAST | Managed artifact declaration; layout may be optimized. |
| ✅ | `Name :: data { ... }` | TAST/IRE | Stable data struct declaration; field order/layout is preserved for ABI/data use. |
| ✅ | `Name :: namespace { ... }` | RAST/TAST | Namespace/module declaration. |
| ✅ | `Name :: enum { ... }` | TAST | Enum declaration. |
| ✅ | top-level executable statement | AST/IRE | Normalized into implicit script entry. |
| ❌ | assign to immutable binding | TAST | Rejected. |
| ❌ | duplicate/conflicting declaration | RAST/TAST | Rejected by symbol/member resolution. |

### Types

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | `void` | TAST | No result type. |
| ✅ | `bool` | TAST | Boolean. |
| ✅ | `char` | TAST | Character scalar. |
| ✅ | `string` | TAST | String reference/value. |
| ✅ | `i8 i16 i32 i64` | TAST | Signed integers. |
| ✅ | `u8 u16 u32 u64` | TAST | Unsigned integers. |
| ✅ | `f32 f64` | TAST | Floating point. |
| ✅ | `Name` | RAST/TAST | Artifact, enum, module member type, or imported type. |
| ✅ | `T{N}` | TAST | Fixed-size array. |
| ✅ | `T[]` | TAST | Growable list. |
| ✅ | `T[][]`, `T{N}[]`, etc. | TAST | Nested arrays/lists where supported by lowering/runtime. |
| ✅ | `fn Ret (params)` | TAST | Procedure/function value type. |
| ✅ | `T*`, `T**` | TAST | Pointer type surface for supported ABI/member paths. |
| ◐ | `Name<T, ...>` | CAST/TAST | Generic type syntax is parsed; semantics are limited to supported compiler paths. |
| ❌ | `i128`, `u128` | CAST/TAST | Not part of the language surface. |
| ❌ | module value as type | RAST/TAST | Rejected. |

### Literals

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | `123`, `0x7B`, `0b1010` | TAST | Integer literal, context typed. |
| ✅ | `1.0`, `3.14` | TAST | Float literal, context typed. |
| ✅ | `true`, `false` | TAST | Bool literal. |
| ✅ | `'A'`, `'\x41'` | Lexer/TAST | Char literal. |
| ✅ | `"text"`, `"A\x42"` | Lexer/TAST | String literal. |
| ✅ | `[a, b, c]` | TAST/IRE | List literal in typed context. |
| ✅ | `{ .x = 1, .y = 2 }` | TAST/IRE | Named artifact literal in typed context. |
| ✅ | `{ 1, 2 }` | TAST/IRE | Positional artifact/array literal where target type disambiguates. |
| ❌ | malformed escape/string/char | Lexer | Rejected. |
| ❌ | literal outside supported target type | TAST/IRE | Rejected. |

### Expressions

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | `name` | RAST/TAST | Identifier lookup. |
| ✅ | `a.b` | RAST/TAST | Module/member/field access. |
| ✅ | `a->b` | TAST | Pointer member access for supported pointer types. |
| ✅ | `f(a, b)` | TAST | Function/procedure/native call. |
| ✅ | `x[i]` | TAST | Array/list/string index where supported. |
| ✅ | `@Type(expr)` | TAST/IRE | Primitive cast. |
| ✅ | unary `-`, `!`, `++`, `--` | TAST/IRE | Unary numeric/bool/lvalue ops. |
| ✅ | binary arithmetic/compare/logical/bitwise | TAST/IRE | Type-checked operator expression. |
| ✅ | assignment expression | TAST/IRE | Assignment or compound assignment to lvalue. |
| ✅ | function literal | TAST/IRE | `fn` literal in typed context. |
| ✅ | switch expression | TAST/IRE | Branch expression with compatible result type. |
| ❌ | bool/char arithmetic | TAST | Rejected. |
| ❌ | char-vs-int comparison | TAST | Rejected. |
| ❌ | indexing non-container | TAST | Rejected. |
| ❌ | non-integer index | TAST | Rejected. |

### Statements and control flow

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | expression statement | TAST/IRE | Evaluates expression, discards value if needed. |
| ✅ | block `{ ... }` | CAST/TAST | Scoped statement list. |
| ✅ | `if cond { ... } else { ... }` | TAST/IRE | Conditional branch. |
| ✅ | `switch expr { ... }` | TAST/IRE | Switch statement/expression. |
| ✅ | `while cond { ... }` | TAST/IRE | Loop. |
| ✅ | `for i : i32 = start .. end { ... }` | TAST/IRE | Range loop. |
| ✅ | `break` | TAST/IRE | Exit loop/switch context. |
| ✅ | `skip` | TAST/IRE | Continue loop. |
| ✅ | `return [expr]` | TAST/IRE | Function return. |
| ✅ | implicit main fallback | AST/IRE | Top-level script/main fallback behavior. |
| ❌ | `break`/`skip` outside valid context | TAST | Rejected. |
| ❌ | missing return where required | TAST | Rejected unless implicit/default path applies. |

### Artifacts, modules, enums, imports, externs

| Status | Syntax | Phase | Meaning / rule |
|:---:|---|---|---|
| ✅ | artifact fields | RAST/TAST | Typed fields with layout metadata. |
| ✅ | artifact methods | RAST/TAST | Methods with `self`. |
| ✅ | module members | RAST/TAST | Namespaced vars/functions/types. |
| ✅ | enum members | TAST/IRE | Enum values lower as supported integer-like values. |
| ✅ | `import Module.Name` | RAST | Import source/module identity. |
| ✅ | `using Module` | RAST | Use reserved/native module member lookup. |
| ✅ | `extern Name : Ret (params)` | TAST/IRE | External call declaration. |
| ✅ | `System.FFI.open` manifest pattern | TAST/IRE | Dynamic-library import support. |
| ❌ | unqualified enum variant | RAST/TAST | Rejected; use `Type.Member`. |
| ❌ | unknown module/member/import | RAST/TAST | Rejected. |
| ❌ | unsupported extern ABI type | TAST | Rejected. |

### Reserved/System/Standard modules

| Status | Module | Surface |
|:---:|---|---|
| ✅ | `Standard.IO` | print/println/format style output paths. |
| ✅ | `Standard.Math` | math constants/functions covered by fixtures. |
| ✅ | `System.Time`, `Standard.Time` | time APIs covered by fixtures/catalog. |
| ✅ | `System.Random`, `Standard.Random` | random APIs covered by fixtures/catalog. |
| ✅ | `System.Thread` | thread APIs covered by fixtures/catalog. |
| ✅ | `System.Channel` | typed channel creation plus send/recv/pending/close variants. |
| ✅ | `System.Env`, `System.OS`, `System.Path`, `Standard.Path`, `System.FS`, `Standard.FS` | environment, OS, path, filesystem/file APIs. |
| ✅ | `System.Json`, `System.Buffer`, `System.Bytes`, `System.Log`, `Standard.Log`, `System.FFI` | JSON/buffer/bytes/log/dynamic-library APIs. |
| ❌ | short reserved imports and unknown reserved members | RAST/TAST | Rejected. |

### Diagnostics and rejection classes

| Status | Class | Phase | Examples |
|:---:|---|---|---|
| ✅ | lexical error | Lexer | invalid char/escape/string. |
| ✅ | parse error | CAST | malformed declarations/blocks/types. |
| ✅ | name error | RAST | unknown identifier/member/import/module. |
| ✅ | type error | TAST | mismatched assignment/call/operator types. |
| ✅ | mutability error | TAST | assign immutable binding/field/global. |
| ✅ | control-flow error | TAST | invalid break/skip/return path. |
| ✅ | ABI error | TAST/IRE | unsupported extern/FFI type/layout. |
| ✅ | lowering error | IRE/IR | unsupported validated construct or backend path. |
| ✅ | runtime trap | Runtime | bounds, null/native/runtime failures. |

## Lexical rules

### Keywords

Most keywords are lowercase:

```txt
while for break skip return if else default switch fn self
module import using extern as true false
```

The declaration-kind keywords accept the capitalized forms used by existing fixtures:

```txt
artifact
enum
namespace
```

Use `module` for file/module headers. Use `Name :: namespace { ... }` for in-language namespace objects.

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

A file may start with an optional module header and may then contain imports, extern declarations, top-level declarations, and top-level script statements.

```simple
module Examples.Math
import Standard.Math

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

## File/module headers

A file may start with a module header:

```simple
module Tools.Widget
```

The module header gives the file an import-index name. It does **not** declare a runtime namespace object and it does **not** wrap the declarations that follow it.

```simple
module Tools.Widget

widgetValue : i32 () {
  return 42
}
```

Another file can import that module name:

```simple
import Tools.Widget

main : i32 () {
  return widgetValue()
}
```

Rules:

- The header keyword is `module`.
- The module name is an identifier path, for example `Main`, `Lib`, or `Tools.Widget`.
- The header should appear before imports/declarations/statements.
- Only import indexing uses the module name; ordinary language lookup still uses declarations, imports, modules, and `using`.
- `Name :: namespace { ... }` is the declaration form for language namespace values; headers never use braces.

Use `Name :: namespace { ... }` only when you want a language namespace value:

```simple
Math :: namespace {
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
Point :: artifact { x : i32; y : i32 }
Math :: namespace { one : i32 () { return 1 } }
Color :: enum { Red = 1, Green = 2 }
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
i8 i16 i32 i64
u8 u16 u32 u64
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

Strings are first-class heap values. `==` and `!=` compare string contents:

```simple
main : i32 () {
  s : string = "hello"
  if (s == "hello") { return len(s) }
  return 0
}
```

IO formatting validates placeholder calls. Examples use standard-library printing:

```simple
import Standard.IO

main : void () {
  Standard.IO.print("answer={}", 42)
  Standard.IO.println(" done")
}
```

Formatting and standard-library calls are part of the standard library surface summarized below.

## Artifacts

Artifacts are record-like declarations with fields and methods:

```simple
Point :: artifact {
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
Counter :: artifact {
  value : i32

  inc : void () {
    self.value += 1
  }
}
```

Artifact ABI flattening is used for supported extern/FFI cases. Recursive artifact ABI is rejected.

## Modules

Namespaces group variables and functions under a named scope:

```simple
Math :: namespace {
  base :: i32 = 2

  add : i32 (a : i32, b : i32) {
    return a + b
  }
}

main : i32 () {
  return Standard.Math.add(Standard.Math.base, 3)
}
```

Unknown module members, using modules as types, and assigning to immutable module variables are rejected.

## Enums

Enums use `:: enum` and require qualified access:

```simple
Color :: enum {
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
import Standard.IO
import Standard.FS as FileSystem
import System.IO
```

`using` imports members into unqualified call scope:

```simple
import System.Channel
using System.Channel

main : i32 () {
  ch : i64 = newI32()
  sendI32(ch, 9)
  return recvI32(ch)
}
```

CLI import resolution handles project-root imports, relative imports, module-map entries, reserved imports, missing imports, ambiguous imports, and cycle detection. Generated `simple.modules` files are build artifacts.

## Reserved/System modules and standard library

`System.*` is canonical for low-level runtime modules. `Standard.*` is the high-level library root that wraps or composes `System.*`. There are no public short compatibility aliases. See `Docs/System.md`, `Docs/Standard.md`, and `Docs/LibraryMigration.md` for the no-alias model.

The standard library is part of the language-facing runtime surface. Reserved imports map onto enum-backed catalog entries and native-backed runtime modules; no implicit ABI coercion is performed. If a module/member is not listed in the catalog or covered by tests, treat it as unsupported.

### Import model

Valid reserved imports use only canonical roots:

```simple
import System.FFI
import System.FS
import System.OS
import System.Channel
import Standard.IO
import Standard.Math
```

Short imports such as `IO`, `FS`, `DL`, `Time`, `Buffer`, and `Channel` are rejected with diagnostics that point to canonical replacements.

### Core modules

| Module | Examples / members |
|---|---|
| `Standard.Math` | `abs`, `min`, `max`, `sqrt`, `PI` |
| `Standard.IO` | `print`, `println`, `readLine` |
| `System.IO` | low-level stream handles and buffer compatibility helpers |
| `System.Time` | `monoNs`, `wallNs`, `sleepNs`, `sleepMs` plus compatibility `mono_ns`/`wall_ns` |
| `System.FS` | file descriptors and low-level file/dir helpers including `readText`, `writeText`, `readBytes`, `writeBytes`, `listDir` |
| `Standard.FS` | high-level file helpers backed by `System.FS` |
| `System.OS` | `platform`, `arch`, process facts, `sleepMs` |
| `System.Env` | args/env/executable path helpers |
| `System.Path` / `Standard.Path` | low-level and ergonomic path helpers |
| `System.Random` / `Standard.Random` | raw RNG and high-level random helpers |
| `System.Thread` | low-level thread helpers |
| `System.Log` / `Standard.Log` | sink/level/file control and high-level log helpers |
| `System.Buffer` / `System.Bytes` | low-level mutable buffers and byte helpers |
| `Standard.Buffer` / `Standard.Bytes` | high-level buffer/byte helper modules, reserved as catalog modules |
| `System.Json` | low-level JSON handles |
| `System.Channel` | typed channel creation plus `send*`, `trySend*`, `recv*`, `tryRecv*`, `pending*`, `close` |
| `System.FFI` | `open`, `sym`, `symbol`, `close`, `lastError`, `supported`, scalar dynamic-call helpers |

`using ModuleName` exposes module members for unqualified calls where that module supports it:

```simple
import System.Bytes
using System.Bytes

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

## Extern declarations and FFI ABI

Extern declarations describe imported host or dynamic-library functions:

```simple
extern puts : i32 (s : string)
extern Ray.InitWindow : void (w : i32, h : i32)
extern ffi.simple_add_i32 : i32 (a : i32, b : i32)
```

Extern names may be module-qualified. Calls are checked for argument count and type compatibility.

Dynamic-library usage is exposed through the canonical `System.FFI` runtime API. Example shape from fixtures:

```simple
import System.FFI

extern ffi.simple_add_i32 : i32 (a : i32, b : i32)

lib :: i64 = System.FFI.open("Tests/ffi/libsimpleffi.so", ffi)

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
- recursive artifact ABI for extern/FFI
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
