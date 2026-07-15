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
module Examples.Reference

import Standard.IO

Point :: data {
  x : i32
  y : i32
}

Counter :: artifact {
  value : i32

  inc :: i32 () {
    return self.value + 1
  }
}

add :: i32 (a : i32, b : i32) {
  return a + b
}

main :: i32 () {
  p : Point = { .x = 3, .y = 4 }
  c : Counter = { .value = 6 }
  Standard.IO.println("next={}", c.inc())
  return add(p.x, p.y)
}
```

Important syntax facts:

- `name : Type` declares a **mutable** binding.
- `name :: Type` declares an **immutable** binding; functions and methods use this form.
- `module Name` is required and declares the source unit's runtime module namespace and named-import identity, including for script-style files.
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
program        = module-header-decl { import-decl | using-decl | extern-decl | top-decl | stmt } ;
module-header-decl   = "module" qualified-name ;
import-decl    = "import" ( qualified-name | string-literal ) [ "as" ident ] ;
using-decl     = "using" qualified-name ;
extern-decl    = "extern" [ qualified-name ] function-signature ;
top-decl       = var-decl | func-decl | artifact-decl | data-decl | namespace-decl | enum-decl ;

var-decl       = ident (":" | "::") type [ "=" expr ] ;
func-decl      = ident (":" | "::") type "(" [ params ] ")" block ;
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
| ✅ | `name :: Ret (params) block` | TAST | Immutable function or method declaration; canonical for functions that are not reassigned. |
| ✅ | `name : Ret (params) block` | TAST | Mutable function or method declaration when reassignment is intended. |
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
| ✅ | `Name<T, ...>` | CAST/TAST/IRE | Concrete named generic types and calls are monomorphized before SIR. |
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

Use `module` for the required runtime module namespace and import identity. Use `Name :: namespace { ... }` for explicitly grouped declarations within a module.

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
module Examples.Reference

main :: i32 () { x : i32 = 1; y : i32 = 2; return x + y; }
```

### Numeric literals

Integer literals support decimal, hex (`0x10`), and binary (`0b1010`) forms where integer parsing is used, including fixed-size array dimensions.

Floating literals are context-typed as `f32` or `f64` by expected type.

### String and character escapes

Strings and chars support normal escapes and hex escapes. Invalid escapes are rejected by lexer tests.

```simple
module Examples.Reference

main :: i32 () {
  c : char = '\x41'
  s : string = "A\x42"
  return 0
}
```

## Program structure and entry points

Every `.simple` source file starts with a module header naming the source unit, followed by imports, extern declarations, top-level declarations, or top-level script statements. Script-style files also require module names; top-level statements still execute through the implicit script entry.

```simple
module Examples.Math
import Standard.Math

square :: i32 (x : i32) {
  return x * x
}

main :: i32 () {
  return square(7)
}
```

Entry behavior:

- If top-level executable statements exist, they are normalized into an implicit script entry.
- If no top-level executable statements exist and `main` exists, `main` is used as the entry.
- Top-level `return` is invalid.
- A `main :: void ()` function is valid; a missing explicit return is valid for `void`.

## File/module headers

Every source file starts with a module header:

```simple
module Tools.Widget
```

The required module header declares the source file's runtime module namespace and import-index identity.

```simple
module Tools.Widget

Widgets :: namespace {
  widgetValue :: i32 () {
    return 42
  }
}
```

Another file can import that module name:

```simple
module App.Main

import Tools.Widget

main :: i32 () {
  return Widgets.widgetValue()
}
```

Rules:

- The header keyword is `module`.
- The module name is an identifier path, for example `Main`, `Lib`, or `Tools.Widget`.
- The required header appears before imports, declarations, and statements.
- The module name is the source unit's runtime namespace and import identity.
- Named imports resolve module headers or module-map entries.
- Quoted imports resolve explicit source paths.
- `Name :: namespace { ... }` groups declarations inside the current module; module declarations never use braces.

Use `Name :: namespace { ... }` when declarations need an additional named group:

```simple
Math :: namespace {
  one :: i32 () { return 1 }
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

A variable declaration may omit an initializer. `v0.5.15` applies deterministic
ZII defaults to supported types, including global/local optional and Result
storage, and forbids reading inactive tagged payloads. A defined zero state is
not permission to perform an invalid operation: for example, a zero raw pointer
is non-dereferenceable and an inactive tagged payload is unreadable.

```simple
x : i32      // zero
missing : i32? // absent
```

### Functions

```simple
add :: i32 (a : i32, b : i32) {
  return a + b
}
```

Function declarations use name-first syntax:

```txt
name :: ReturnType (params...) { body }
```

The marker before the return type also carries return mutability facts used by validation.

### Top-level declarations with `::`

`::` introduces immutable top-level values and declaration kinds:

```simple
Pi :: f64 = 3.141592
Point :: artifact { x : i32; y : i32 }
Math :: namespace { one :: i32 () { return 1 } }
Color :: enum { Red = 1, Green = 2 }
```

## Mutability

Mutability is part of declarations and parameters:

```simple
module Examples.Reference

main :: i32 () {
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
use :: i32 (x : i32, y :: i32) {
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

The `v0.5.17` compiler materializes concrete top-level and namespace-owned
generic functions, generic methods, artifacts/data, fields, globals, module
variables, nested types, procedure signatures, and quoted-source imports before
SIR emission. Type arguments may be explicit or inferred from independently
typed call arguments. A contextual literal or lambda may consume an already
inferred type, but does not by itself infer a missing type argument; an explicit
type argument is required when every inference source is contextual.

Specializations have deterministic IR-safe symbols, duplicate requests reuse
one body/layout, and dependencies are discovered after substitution. Adjacent
nested call closers are canonical (`head<Box<i32>>(values)`); whitespace is not
required to distinguish them from the shift operator. Nested concrete
identities are resolved before inner arguments are rewritten, so compositions
such as `Pair<i32, Box<i32>>` retain their identity. Procedure identities include
return mutability and pointer modifiers. Fixed-array/list modifiers introduced
around a type parameter remain outer modifiers: substituting `T = i32{2}` into
`T{3}` produces `i32{3}{2}`.

Namespace specializations remain inside their owner. Generic methods combine
receiver and method arguments into one identity and work on named, indexed,
temporary, global, and namespace-variable receivers. Generic functions may call
generic methods, including multiple specializations originating at one source
location. Recursive and mutually recursive calls reuse concrete bodies;
recursion that continually grows its type arguments is rejected as
non-terminating specialization rather than expanding to an arbitrary limit.
Concrete artifacts materialize before dependent methods, so chained calls do
not depend on source request order. Managed paths not accepted by direct LLVM
lowering fall back before execution, never after a partial JIT transition. The
`v0.5.15` implementation materializes optional `T?` and `Result<T,E>` as
deterministic concrete managed layouts. Optional
absence uses the zero reference state; presence stores the substituted payload. Result stores an `i32` tag plus
substituted value and error payload fields. Nested wrappers, artifacts, lists,
generic functions, namespaces, and quoted imports compose through the same
specialization pipeline.

Postfix `T?` directly replaces the experimental generic optional name; no alias
or compatibility lowering remains. Contextual optional and Result literals,
exhaustive structural patterns, and postfix propagation are implemented across
validation, specialization, SIR/SBC emission, verification, the interpreter,
and LLVM JIT fallback. `Promise<T>`, `async`, and `await` remain
language-completion work.

This breaking transition sets language syntax to 2.0, SIR to 2.0, SBC and
opcode metadata to version 2, runtime ABI to 1.2, and the standard-library
catalog to 2.0. Versioned old inputs are rejected; no translation shim or alias
is retained. The completed lambda grammar advances the current language syntax
version to 2.1; SIR remains 2.0 because lambda bodies lower through existing
function, closure, and indirect-call instructions.

### `v0.6` generic design

Generics are part of language completion, not deferred library work. Functions,
data/artifacts, fields, procedure types, and canonical wrappers use the same type
parameter syntax:

```simple
identity<T> :: T (value : T) {
  return value
}

Box<T> :: artifact {
  value : T
}
```

The `v0.6` implementation model is concrete monomorphization. Every used type
argument combination produces one deterministic concrete specialization before
SIR/SBC emission. There is no dynamic `any`, runtime overload guessing, erased
payload, or implicit coercion between specializations.

Generic completion requires:

- deterministic type identity and symbol mangling;
- specialization requests from annotations, calls, literals, fields, globals,
  imports, and native/library signatures;
- exact substitution through nested types such as
  `Promise<Result<T?, E>>`;
- invariant mutable containers and wrappers unless variance is explicitly
  designed later;
- rejection of recursive value containment without pointer/ref/handle
  indirection;
- complete layout, verifier, interpreter, JIT, GC-root, and diagnostic parity.

`Result<T,E>` and `Promise<T>` are canonical generic language types. Optional
`T?` is a canonical postfix type constructor implemented by the same concrete
specialization/layout machinery, not a public `Option<T>` generic or an ad hoc
backend exception.

## `v0.6` language-completion scope

Language completion includes all of the following as one dependency-ordered
milestone family:

1. complete concrete generics across every supported language boundary;
2. complete lambda expressions, callable typing, and generic lambda behavior (completed in v0.5.18);
3. complete closures with captured lexical state, escaping lifetimes, mutable
   sharing, and precise GC rooting;
4. ZII, `Result<T,E>`, optional type `T?`, postfix expression `expr?`,
   exhaustive optional patterns, and deterministic cleanup;
5. `Promise<T>`, prefix `await`, `async` functions, and resumable execution;
6. external-C ABI types, raw/optional/function pointers, provenance, ownership,
   and lifetime rules;
7. a conformance burn-in that resolves known parser, type-system, lowering,
   verifier, interpreter, JIT, GC-root, and diagnostic discrepancies before the
   syntax contract is frozen.

The async/error design below depends on complete generic, lambda, and closure
work; it does not replace or postpone it. Native and standard library expansion
starts only after these language gates pass, so later releases do not need to
redesign ordinary source syntax.

## Async functions and explicit failure design

> **Completion status:** optional `T?`, Result contextual literals/patterns,
> and postfix propagation are implemented in `v0.5.15`. The `async` and `await`
> placement and semantics in this section remain the accepted `v0.6` design
> target, not current functionality. No constructor names are implied. The
> transitional `System.Job` and `Standard.Promise` calls remain documented in
> [Jobs, promises, and async design](Async.md).

### `async` return modifier

`async` is a prefix modifier on a function's declared return type. It appears
immediately after the function's `:` or `::` marker, where return-type markers
already belong:

```simple
module Example.Fetch

import Standard.HTTP

fetchBody :: async Result<string, HttpError> (url : string) {
  response :: Response = await Standard.HTTP.get(url)?
  return response.bodyText()
}
```

The written return type is the function body's real result type. It is not
written as `Promise<Result<string, HttpError>>`. Calling the function produces
`Promise<Result<string, HttpError>>`; the compiler performs that wrapping as
part of async lowering.

`async` marks a function as suspendable. It is not part of a library member's
name and does not create names such as `getAsync`. Both mutable (`:`) and
immutable (`::`) function declarations may carry the modifier.

### `await`

`await` is a prefix expression keyword. Given `Promise<T>`, it suspends the
enclosing async function until the promise completes and then produces `T`:

```simple
value :: Result<Response, HttpError> = await Standard.HTTP.get(url)
```

`await` is legal only within a function declared `async`. Awaiting a non-promise
value, or using `await` in an ordinary function, is a compile error. Suspension
must preserve typed locals, control-flow position, live GC roots, and owned
resources; it must not block a VM worker thread as an implementation shortcut.

### `Result<T, E>`

`Result<T, E>` represents expected success or failure. The error type is always
explicit; there is no default error type and no exception path hidden beside
the return type.

```simple
readConfig :: Result<string, IoError> () {
  // Produces either a success payload or an error payload.
}
```

Its two semantic states carry `T` in `.value` on success and `E` in `.error` on
failure. Construction and exhaustive handling reuse contextual named literals
and structural patterns:

```simple
success : Result<i32, IoError> = { .value = 42 }
failure : Result<i32, IoError> = { .error = ioError }

switch (success) {
  { .value = value } => use(value)
  { .error = error } => handle(error)
}
```

Under ZII, the all-zero Result state is `{ .value = zero(T) }`; `.error` is
inactive. This value default is not evidence that an asynchronous producer
completed: an enclosing Promise state controls whether any Result payload is
active. Missing function returns remain diagnostics.

Result has no constructor names. The `.value` and `.error` labels are contextual
literal/pattern metadata, not unchecked member access; payloads become ordinary
values only through an exhaustive pattern or `?`. Failure is an ordinary typed
return value. Nothing throws and no stack unwinds invisibly. A Result-producing
expression cannot be silently treated as `T`; consuming code must branch, propagate with `?`, return/store the
Result. A Result used as a bare expression statement is a diagnostic; the
language currently has no implicit or silent discard path.

### ZII and optional `T?`

Simple uses Zero Is Initialization (ZII): every storage location is fully
zero-initialized and every type defines a deterministic zero state. Zeroed
bytes belonging to an inactive tagged payload are storage, not an active value;
they are never read, traced, compared, or cleaned up as that payload.

Postfix type `T?` represents either absence or one present `T`. There is no
public `Option<T>` type, null type, null literal, or constructor-name API. The
`v0.6` grammar separates type and expression postfix contexts:

```ebnf
postfix-type     = primary-type { pointer-suffix | dimension-suffix | "?" } ;
postfix-expr     = primary-expr { call | member | index | "?" } ;
optional-literal = "{" [ expr ] "}" ;
optional-pattern = "{" [ ident ] "}" ;
```

The all-zero optional state is absent. A separate discriminator makes present
`zero(T)` distinct from absence:

```simple
missing : i32?             // absent by ZII
alsoMissing : i32? = {}    // explicit absence
presentZero : i32? = { 0 } // present i32 zero
present : i32? = { 42 }
```

Contextual optional literals are explicit: `{}` constructs absence and
`{ expression }` constructs presence. Simple does not implicitly convert `T`
to `T?`.

Postfix type modifiers apply in written order:

```simple
values : i32?[]  // list of optional i32 values
maybeValues : i32[]? // optional list of i32 values
outerMissing : i32?? = {}
innerMissing : i32?? = { {} }
presentZero : i32?? = { { 0 } }
```

Every `?` contributes its own discriminator, so the three nested examples are
distinct.

Optional values use exhaustive structural patterns rather than named
constructors:

```simple
findUser :: User? (id : i32) {
  if (!userExists(id)) { return {} }
  return { loadUser(id) }
}

userName :: string (candidate : User?) {
  switch (candidate) {
    { user } => return user.name
    {} => return "missing"
  }
}
```

The `{ binding }` branch handles presence and binds `T`; the `{}` branch handles
absence. Missing or duplicate states are diagnostics. Direct wrapper payload
member access is rejected so an inactive payload cannot be observed. Tagged
wrappers do not define implicit whole-value equality; branch on the state and
compare an active payload using that payload type's ordinary operations.

### Expression `expr?` propagation

A postfix `?` in expression context resolves optional `T?` or `Result<T,E>` at
the expression where it appears. Type `T?` and expression `expr?` are distinct
parser contexts.

- a present `T?` produces its `T` payload;
- an absent `T?` immediately returns absence from an enclosing `U?` function;
- a successful Result produces its value payload;
- a Result error immediately returns the same typed error state from an
  enclosing `Result<U,E>` with the same `E`.

```simple
getDataFromAlgo :: async i32? () {
  if (!algorithmHasResult()) { return {} }
  return { calculateResult() }
}

doubleData :: async i32? () {
  result :: i32? = await getDataFromAlgo()
  value :: i32 = result?
  return { value * 2 }
}
```

If `result` is absent, `doubleData` completes with absence and executes no later
statement. If it is `{ 0 }`, `value` is the legitimate integer zero. Extraction
does not mutate the optional value.

Using optional `expr?` in a function returning plain `U`, or Result `expr?` in a
function with a different error type, is a compile error. Callers that cannot
propagate must branch exhaustively. There is no implicit conversion, invented
default, or discarded error. Propagation uses ordinary immediate return control
flow and preserves managed roots; it does not introduce exceptions or a hidden
unwind path. The current source language has no user-defined destructor, so no
separate destructor order is invented for `?`.

### Composition

`await` resolves asynchronous completion and expression `?` resolves failure or
absence. They compose in one expression:

```simple
response :: Response = await Standard.HTTP.get(url)?
```

This is equivalent to:

```simple
response :: Response = (await Standard.HTTP.get(url))?
```

The call returns `Promise<Result<Response, HttpError>>`; `await` produces the
`Result`, and `?` either produces `Response` or returns the same typed
`HttpError` state from the enclosing function. The expression grammar must
preserve this ordering rather than applying `?` to the promise.

Async library APIs use natural operation names and explicit promise result
types. LSP completion, hover, and signature help will label an operation as
asynchronous and show `Promise<T>`; APIs will not encode async behavior in
names such as `runAsync`, nor provide `.await()` library methods. Read-only
pseudo-source definitions for editor navigation are specified in
[Library pseudo-sources](library/README.md).

### Promise completion, errors, and cancellation

`Promise<T>` is its own language type and owns its asynchronous completion
state. Its target states are:

- `Pending`;
- `Completed(T)`;
- `Cancelled`.

The all-zero `Promise<T>` state is terminal `Cancelled`: it has no producer and
no active payload. The runtime explicitly initializes a live Pending promise,
and only an atomic producer transition may activate `Completed(T)`. Pending and
Cancelled payload bytes remain inactive even though ZII has zeroed their
storage.

Consequently, for `Promise<i32?>`, no completion, completed absence, and
completed present zero remain distinct:

| Promise state | Result of `await` |
|---|---|
| Pending | suspend; produce no value |
| Cancelled | propagate structured cancellation |
| Completed with `{}` | produce absent `i32?` |
| Completed with `{ 0 }` | produce present integer zero |

`Completed` means the producer finished and supplied its declared `T`; it does
not mean that a domain operation succeeded. A fallible producer uses
`Promise<Result<Value, Error>>`, which can therefore finish with either the
Result's success payload or its typed error payload. The Promise does not
inspect, catch, or reinterpret that Result.

```simple
result :: Result<Response, HttpError> = await Standard.HTTP.get(url)
response :: Response = result?
```

The usual combined form is `await Standard.HTTP.get(url)?`, equivalent to
`(await Standard.HTTP.get(url))?`. The user handles a completed error state by
exhaustively branching on the Result or propagates it with `?`. An async
function declared `async Result<T,E>` completes its returned Promise with an
ordinary Result carrying either `T` or `E`.

Expected operation failures do not create an untyped failed/rejected Promise
state. Cancellation is different from a domain error and does not require every
error enum to add a `Cancelled` variant. A plain `Promise<T>` is appropriate
only when normal completion is infallible; recoverable producer failures require
a Result (or another explicit sum type) inside `T`.

When `await` observes `Completed(value)`, it produces `value`. When it observes
`Pending`, it suspends and registers the continuation; it never reads the
zeroed inactive payload as a completed value. When it observes
`Cancelled`, it cancels the Promise of the enclosing async function, runs that
frame's required cleanup, and executes no later statement in the frame. It does
not synthesize `T`, convert cancellation to a Result error state, or throw an
exception.
Cancellation therefore propagates through an await chain as Promise state until
code explicitly observes or isolates it through Promise control APIs.

Resolution and cancellation race atomically; exactly one terminal state wins.
Cancellation requests are idempotent, wake suspended continuations, and flow to
the currently awaited child unless an explicit future shielding/detachment API
says otherwise. A managed `Promise<T>` does not require public `close`; the VM
retains it while pending/referenced and releases its runtime state after terminal
observation and GC reachability permit cleanup.

Resource cleanup remains mandatory across cancellation and suspension. Runtime
traps are programmer/runtime faults, not expected Promise failures, and remain
separate from `Result` and structured cancellation.

## Procedure and pointer types

### Procedure types

```simple
fn i32 ()
fn bool (i32, string)
fn i32 (a : i32, b : i32)
```

Procedure values are supported in local, global, and namespace variables; parameters and returns; generic specializations; switch expressions; artifact members; and list/fixed-array elements. They remain rejected at extern ABI boundaries because VM callables are managed references, not external-C function pointers.

### Pointer types

```simple
i32*
void**
i32*?                 // optional raw pointer
fn i32 (i32)*?        // optional external function pointer
```

The current compiler parses `T*`/`T**` and validates supported `->` paths. The
complete ZII, nullability, provenance, ownership, lifetime, callback, and
external-C rules are the `v0.6` design specified under
[Pointers and member access](#pointers-and-member-access).

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
module Examples.Reference

add :: i32 (a : i32, b : i32) {
  return a + b
}

main :: i32 () {
  a : i8 = 40
  b : i8 = 2
  return add(@i32(a), @i32(b))
}
```

String conversions use the same syntax where supported:

```simple
module Examples.Reference

main :: string () {
  x : i32 = 42
  return @string(x)
}
```

Using primitive type names as normal functions for casts is rejected. The expected diagnostic contains “primitive cast syntax requires '@'”.

## Statements and control flow

### Return

```simple
module Examples.Reference

main :: i32 () {
  return 0
}
```

Non-void functions must return on all required paths. `void` functions may fall through.

### If / else

```simple
module Examples.Reference

main :: i32 () {
  x : i32 = 7
  if (x > 5) {
    return 1
  } else {
    return 0
  }
}
```

Condition chains use `|>` branches. They are checked for return flow and can end with `|> default`:

```simple
scoreLabel :: string (score : i32) {
  |> (score >= 90) { return "great" }
  |> (score >= 70) { return "solid" }
  |> default { return "needs work" }
}
```

Nested `if`/`else` chains are also normalized and checked for return flow, but `|>` is the preferred syntax when documenting chain-style control flow.

### While

```simple
module Examples.Reference

main :: i32 () {
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
module Examples.Reference

main :: i32 () {
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
module Examples.Reference

main :: i32 () {
  x : i32 = 1
  return switch (x) {
    x > 0 => { local : i32 = 10; return local }
    default => return 0
  }
}
```

Switch branch locals are scoped to the branch. Validation checks branch shapes and result compatibility.

## Built-in functions and forms

Simple keeps built-ins small and explicit. Library work belongs under `System.*` and `Standard.*`; language built-ins cover core operations that need compiler knowledge.

### `len(value)`

Returns the length of a string, fixed array, or list as `i32`:

```simple
name : string = "Simple"
letters : i32 = len(name)

fixed : i32{3} = {1, 2, 3}
fixedCount : i32 = len(fixed)

items : i32[] = [10, 20, 30]
itemCount : i32 = len(items)
```

### Explicit primitive casts: `@Type(value)`

Primitive conversions must be requested explicitly. Calls like `i32(value)` are rejected; use `@i32(value)`. This is how Simple enforces no implicit coercion.

```simple
wide : i64 = 42
narrow : i32 = @i32(wide)

ratio : f64 = 3.5
whole : i32 = @i32(ratio)

text : string = "42"
parsed : i32 = @i32(text)
```

Supported cast targets are primitive scalar types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `char`, and `string`.

### List methods

Growable lists expose checked methods for common mutations and queries:

```simple
items : i32[] = []
items.push(10)
items.push(20)

count : i32 = items.len()
last : i32 = items.pop()

items.insert(0, 5)
removed : i32 = items.remove(0)
items.clear()
```

### Standard output helpers

Printing is library-provided, not a global builtin. Use `Standard.IO` for output and format strings:

```simple
import Standard.IO

Standard.IO.print("answer={}", 42)
Standard.IO.println(" done")
```

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
module Examples.Reference

main :: i32 () {
  s : string = "hello"
  if (s == "hello") { return len(s) }
  return 0
}
```

IO formatting validates placeholder calls. Examples use standard-library printing:

```simple
module Examples.Reference

import Standard.IO

main :: void () {
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

  sum :: i32 () {
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

  inc :: void () {
    self.value += 1
  }
}
```

Artifact ABI flattening is used for supported extern/FFI cases. Recursive artifact ABI is rejected.

## Modules

Namespaces group variables and functions under a named scope:

```simple
module Examples.Reference

Math :: namespace {
  base :: i32 = 2

  add :: i32 (a : i32, b : i32) {
    return a + b
  }
}

main :: i32 () {
  return Math.add(Math.base, 3)
}
```

Unknown module members, using modules as types, and assigning to immutable module variables are rejected.

## Enums

Enums use `:: enum` and require qualified access:

```simple
module Examples.Reference

Color :: enum {
  Red = 1,
  Green = 2
}

main :: i32 () {
  return Color.Green
}
```

Qualified enum members are contextual constants. An expected enum type or the
current `i32` enum representation may consume `Color.Green`, including at a
call-argument site. The member does not independently synthesize a type and
therefore cannot infer a generic type parameter; generic calls need another
independently typed argument or explicit type arguments. Tests reject
unqualified variants (`Green` instead of `Color.Green`), unknown members, and
using the enum type itself as a value.

## Imports and `using`

Imports accept declared module names, explicit quoted source paths, and aliases. A module source can declare its import identity:

```simple
module Graphics.Raylib
```

Other source units can import that module name or request a local path explicitly:

```simple
module App.Main

import Graphics.Raylib
import Graphics.Raylib as Ray
import "./local_helpers"
import "./legacy_helpers.simple"
import Standard.IO
import Standard.FS as FileSystem
import System.IO
```

Quoted paths are resolved as source paths; unquoted qualified names are resolved through module headers, module maps, or canonical `System.*`/`Standard.*` modules.

`using` imports members into unqualified call scope:

```simple
module Examples.Reference

import System.Channel
using System.Channel

main :: i32 () {
  ch : i64 = newI32()
  sendI32(ch, 9)
  return recvI32(ch)
}
```

CLI import resolution handles project-root imports, relative imports, module-map entries, reserved imports, missing imports, ambiguous imports, and cycle detection. Generated `simple.modules` files are build artifacts.

## Reserved/System modules and standard library

`System.*` is canonical for low-level runtime modules. `Standard.*` is the high-level library root that wraps or composes `System.*`. There are no public short compatibility aliases. See `docs/System.md` and `docs/Standard.md` for the library model.

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
| `System.Job` / `Standard.Promise` | transitional scalar jobs with run/spawn, library `await`, poll, cancellation, state queries, and explicit close; target `v0.6` uses typed `Promise<T>` plus the language `await` expression |
| `System.Log` / `Standard.Log` | sink/level/file control and high-level log helpers |
| `System.Buffer` / `System.Bytes` | low-level mutable buffers and byte helpers |
| `Standard.Buffer` / `Standard.Bytes` | high-level buffer/byte helper modules, reserved as catalog modules |
| `System.Json` | low-level JSON handles |
| `System.Channel` | typed channel creation plus `send*`, `trySend*`, `recv*`, `tryRecv*`, `pending*`, `close` |
| `System.FFI` | `open`, `sym`, `symbol`, `close`, `lastError`, `supported`, scalar dynamic-call helpers |

`using ModuleName` exposes module members for unqualified calls where that module supports it:

```simple
module Examples.Reference

import System.Bytes
using System.Bytes

main :: i32 () {
  b : i32[] = new(4)
  return len(b)
}
```

## Functions, procedure types, and function literals

Procedure values use `fn` types:

```simple
module Examples.Reference

main :: i32 () {
  f :: fn i32 (a : i32, b : i32) = (a, b) { return a + b }
  return f(40, 2)
}
```

Function literals use `(parameters) { body }`. Parameters may be target-typed or explicitly typed, as in `(value : i32) { return value + 1 }`. Literals are supported in local, global, namespace, field, argument, return, generic, list, and fixed-array contexts. Nested literals, lexical capture, escaping closures, and direct anonymous invocation are supported when a result type is available from context. Interpreter execution uses heap-backed callable references and rooted capture cells; LLVM rejects captured closure lowering before execution and uses the interpreter path.

Unsupported/rejected procedure cases include:

- procedure values at extern ABI boundaries
- direct anonymous invocation without a typed result context

### `v0.6` lambda and closure design

Lambda expressions are the existing anonymous function literals, not a future
second syntax. The frozen `(parameters) { body }` grammar supports contextual
and explicit parameter typing, direct invocation, nesting, arguments, returns,
globals, namespaces, fields, supported collections, and concrete generic
specialization. Generic lambda behavior uses ordinary monomorphization; no
runtime-erased callable or library-specific lambda syntax is introduced.

Closures complete the callable language surface needed by async jobs, callbacks,
and resource-safe composition. Function
literals keep the existing `fn` procedure type and lexical body syntax:

```simple
main :: i32 () {
  base :: i32 = 40
  addBase :: fn i32 (value : i32) = (value) {
    return base + value
  }
  return addBase(2)
}
```

The implemented semantics are:

- free lexical bindings referenced by the literal are captured automatically;
- immutable bindings have capture-by-value source semantics and cannot be assigned through the closure;
- mutable bindings are captured through a VM-owned rooted cell, so mutations
  are visible to the defining scope and every closure sharing that binding;
- an escaping closure extends the lifetime of its environment and mutable
  cells; it never retains a raw pointer into an expired stack frame;
- nested closures may capture an outer closure's environment;
- closure environments participate in precise GC tracing; capturing a native
  resource handle neither transfers ownership nor adds implicit close behavior,
  so its declared explicit/runtime-owned lifecycle remains unchanged;
- closure values are not implicitly comparable, serializable, or valid at an
  extern/FFI boundary;
- host worker threads do not execute closures or access their environments
  directly; async closure execution resumes on VM-owned scheduler state.

A closure's callable type remains `fn ReturnType (...)`. Capture layout is an
implementation detail recorded in TAST/SIR/SBC metadata, not part of source type
identity. Two literals with the same `fn` signature are callable through that
signature but retain distinct environments.

Closure conformance covers contextual/explicit typing, direct invocation,
immutable and mutable capture, escaping lifetimes, nested and sibling-shared
environments, recursive and generic closures, receiver capture, imports,
managed payloads, and GC pressure. Captured machine-code lowering remains an
explicit pre-execution LLVM fallback; suspension, async callbacks,
cancellation, and suspension cleanup are completed with async/await before
`v0.6.0`.

## Extern declarations and FFI ABI

Extern declarations describe imported host or dynamic-library functions. Extern
names may be module-qualified, and calls are checked for argument count, exact
types, calling convention, and supported ABI layout.

The current `v0.5.15` dynamic-library shape remains transitional:

```simple
module Examples.Reference

import System.FFI

extern ffi.simple_add_i32 : i32 (a : i32, b : i32)

lib :: i64 = System.FFI.open("tests/ffi/libsimpleffi.so", ffi)

main :: i32 () {
  return ffi.simple_add_i32(40, 2)
}
```

The raw `i64` library handle and managed `string` declarations accepted by
specific transitional paths are not the final external-C type model.

### `v0.6` ABI boundary

VM-native calls and external-C calls are different ABIs. VM-native metadata may
name rooted managed values. An external-C declaration may never reinterpret a
VM reference, managed string, list, artifact, closure, Result, Promise, or
general optional value as a host pointer.

External-C scalar mappings are exact:

| Simple type | External-C meaning |
|---|---|
| `i8`/`u8` through `i64`/`u64` | matching fixed-width C integer |
| `f32`/`f64` | C `float`/`double` of matching ABI |
| `bool` | C `_Bool` only when the ABI metadata confirms that mapping |
| `usize`/`isize` | host pointer-width unsigned/signed ABI integer |
| enum | its explicitly declared fixed-width underlying integer |
| `char` | no implicit C `char` mapping; Simple `char` is a Unicode scalar |

`usize` and `isize` have checked portable SBC representation and explicit
host-width marshaling. Platform-dependent C `char`, `short`, `int`, `long`,
enums, bitfields, variadics, and calling conventions are rejected unless the
extern metadata fixes their ABI meaning. The compiler never silently treats a
pointer or `size_t` as `i64`/`u64`.

Stable no-reference `data` values may cross by value when their computed field
layout, alignment, padding, and calling convention match. Managed artifacts,
recursive values, and data containing VM references do not.

## Pointers and member access

Raw pointer types use postfix `*`:

```simple
i32*                  // pointer to i32
i32**                 // pointer to pointer to i32
void*                 // opaque untyped address
i32*?                 // optional pointer to i32
fn i32 (i32)*         // external-C function pointer
fn i32 (i32)*?        // optional external-C function pointer
```

A plain `fn Return(params)` is a VM callable and may carry a closure
environment. `fn Return(params)*` is an external function pointer with no Simple
capture environment. The two representations are never implicitly converted.

### ZII pointer states and C nullability

ZII initializes raw pointer storage to address zero. For non-optional `T*`, that
is a deterministic but non-dereferenceable zero state. Definite-state analysis
rejects dereference, member access, callback invocation, or passage to a
non-null extern parameter until a usable pointer has been assigned. Runtime and
JIT guards trap if an external boundary violates a declared non-null result.
There is still no source `null` type or null literal.

A C pointer that may be address zero is declared `T*?`. At an external-C
boundary only, optional-pointer lowering uses the C null niche:

- absent `{}` marshals as address zero;
- present `{ pointer }` requires a nonzero usable pointer and marshals as that address;
- a C address-zero result becomes absent;
- a nonzero C result becomes present.

This exception does not pass Simple's general tagged optional layout to C.
Scalar `i32?`, aggregate `Data?`, Result, Promise, and other tagged values remain
invalid in direct external-C signatures unless an explicit stable C struct ABI
is declared.

Postfix modifiers apply in written order. `T*?` is an optional pointer, while
`T?*` is a pointer to a Simple optional representation and is not generally
external-C-compatible. Parenthesized type grouping expresses deeper shapes,
such as `(T*?)*` for a pointer to a nullable pointer. Each accepted level must
have an exact ABI layout.

### Pointer operations and mutability

Address-of produces a borrowed pointer with source provenance:

```simple
value : i32 = 42
pointer :: i32* = &value
```

The final pointer surface includes guarded unary dereference, typed indexing
when a proven extent exists, and `->` as member access through a pointer:

```simple
value = *pointer
field = node->value
byte = buffer[index]
```

Raw pointer arithmetic, ordering, and implicit pointer/integer conversion are
not part of the stable language. Equality is allowed only between compatible
pointer forms. Casts use explicit `@Target(pointer)` syntax and preserve
optionality and mutability; only ABI-compatible typed-pointer/`void*`
conversions are accepted.

Declaration markers carry pointer mutability into provenance and extern
metadata. An immutable `::` pointer parameter is an input/read-only pointee
view; mutable `:` permits the declared output or in/out access:

```simple
extern ffi.findByte : u8*? (
  data :: u8*,
  count :: usize,
  needle :: u8
)

extern ffi.copyBytes : void (
  destination : u8*,
  source :: u8*,
  count :: usize
)
```

The compiler rejects writes through immutable provenance even if a later alias
uses a mutable binding.

### Pointer lifetime and ownership

Raw pointers never become GC roots and never own host memory implicitly.
Address-of VM locals and fields is call-scoped by default. Such pointers cannot
escape through a return, global, heap field, closure, worker thread, callback,
or async suspension unless explicit pin/static/owner metadata proves the full
lifetime. Moving managed storage cannot be addressed without pinning.

External pointer results are borrowed by default. Retaining, transferring, or
freeing one requires metadata naming its owner, lifetime, and deallocator.
Long-lived host resources use typed generational handles rather than untracked
raw pointers. Pointer-to-pointer outputs require mutable destination provenance
and the same ownership/nullability validation for the pointer written by C.

Managed `string` never converts implicitly to `u8*` or a C string. C strings,
borrowed string views, and byte views require explicit ABI wrapper/conversion
metadata with encoding, terminator, extent, and call-duration lifetime. A
native function retaining a view must copy it into owned storage.

Dereference, indexing, and `->` require provenance and sufficient known extent.
An unbounded foreign pointer is pass/compare/round-trip only. External C remains
capability-gated because a lying host ABI cannot be made memory-safe by source
types, but malformed declarations and known lifetime/nullability violations are
rejected before execution.

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

- the planned `async` return modifier and `await` expression
- language-level `Promise<T>` layout and execution semantics
- procedure values and closures at extern boundaries
- recursive artifact ABI for extern/FFI
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
