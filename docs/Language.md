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

A variable declaration may omit an initializer; it is zero/default-initialized when the type supports that path.

```simple
x : i32
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

The experimental `v0.5.8` compiler materializes concrete top-level and
namespace-owned generic function and artifact specializations before SIR
emission. Type arguments may be
explicit or inferred from call arguments. Specializations have deterministic
IR-safe symbols, duplicate requests reuse one body/layout, nested generic
dependencies are discovered after substitution, and concrete scalar, string,
procedure, nested artifact, list-wrapped artifact, and quoted-source-import
cases execute through the normal compiler/runtime pipeline. Nested concrete
identities are resolved before their inner type arguments are rewritten, so
compositions such as `Pair<i32, Box<i32>>` retain deterministic specialization
identity. Managed generic calls that are not yet safe for direct LLVM lowering
fall back to the interpreter before execution rather than trapping after a
partial JIT transition. Namespace-owned generic functions preserve ownership
through materialization and work across quoted imports; their concrete bodies
remain inside the owning namespace rather than leaking synthetic top-level
symbols.

Generic methods and executable canonical tagged `Result`/`Option`/`Promise`
values remain language-completion work. `v0.5.8` reserves those three canonical
generic type names and validates
exact arity (`Result<T,E>`, `Option<T>`, and `Promise<T>`), but does not yet
provide their constructors, payload operations, or runtime lowering.

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
  `Promise<Result<Option<T>, E>>`;
- invariant mutable containers and wrappers unless variance is explicitly
  designed later;
- rejection of recursive value containment without pointer/ref/handle
  indirection;
- complete layout, verifier, interpreter, JIT, GC-root, and diagnostic parity.

`Result<T,E>`, `Option<T>`, and `Promise<T>` are canonical generic language types,
not unrelated hard-coded exceptions to the generic system.

## `v0.6` language-completion scope

Language completion includes all of the following as one dependency-ordered
milestone family:

1. concrete generics and canonical tagged generic layouts;
2. `Result<T,E>`, `Option<T>`, and postfix `?`;
3. `Promise<T>`, prefix `await`, and `async` functions;
4. closures with captured lexical state;
5. deterministic resource cleanup across return, propagation, suspension, and
   cancellation.

The async/error design below depends on the generic and closure work; it does
not replace or postpone it.

## Async functions and explicit failure design

> **Design target for `v0.6`:** the syntax and semantics in this section are the
> accepted language design, not functionality provided by the current `v0.5.8`
> compiler. The transitional `System.Job` and `Standard.Promise` calls remain
> documented in [Jobs, promises, and async design](Async.md).

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
  // Produces Ok(string) or Err(IoError).
}
```

Its two states are `Ok(T)` and `Err(E)`. Failure is an ordinary typed return
value. Nothing throws and no stack unwinds invisibly. A Result-producing
expression cannot be silently treated as `T`; consuming code must branch,
propagate with `?`, return/store the Result, or explicitly discard it through a
future deliberate discard form. An accidental unused Result is a diagnostic.

### `Option<T>`

`Option<T>` represents presence or absence without null:

```simple
findUser :: Option<User> (id : i32) {
  // Produces Some(User) or None.
}
```

Its two states are `Some(T)` and `None`. `None` carries no hidden default value.

### `?` propagation

`?` is a postfix propagation operator. It resolves a `Result` or `Option`
immediately at the expression where it appears:

- `Ok(value)?` and `Some(value)?` produce the plain inner value;
- `Err(error)?` immediately returns `Err(error)` from the enclosing function;
- `None?` immediately returns `None` from the enclosing function.

Nothing after a propagated failure or absence executes. The value bound after
`?` is never an unresolved `Result` or `Option`.

For `Result<T, E>`, `?` is legal only when the enclosing function returns
`Result<U, E>` with the same error type. For `Option<T>`, it is legal only when
the enclosing function returns `Option<U>`. Using `?` in any other return shape
is a compile error; there is no implicit conversion, default, or discarded
error.

### Composition

`await` resolves time and `?` resolves failure. They compose in one expression:

```simple
response :: Response = await Standard.HTTP.get(url)?
```

This is equivalent to:

```simple
response :: Response = (await Standard.HTTP.get(url))?
```

The call returns `Promise<Result<Response, HttpError>>`; `await` produces the
`Result`, and `?` either produces `Response` or returns `Err(HttpError)` from
the enclosing function. The expression grammar must preserve this ordering
rather than applying `?` to the promise.

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

`Completed` means the producer finished and supplied its declared `T`; it does
not mean that a domain operation succeeded. A fallible producer uses
`Promise<Result<Value, Error>>`, which can therefore finish as either
`Completed(Ok(value))` or `Completed(Err(error))`. The Promise does not inspect,
catch, or reinterpret that Result.

```simple
result :: Result<Response, HttpError> = await Standard.HTTP.get(url)
response :: Response = result?
```

The usual combined form is `await Standard.HTTP.get(url)?`, equivalent to
`(await Standard.HTTP.get(url))?`. The user handles a completed `Err` by
branching on the Result or propagates it with `?`. An async function declared
`async Result<T,E>` completes its returned Promise with either `Ok(T)` or
`Err(E)`.

Expected operation failures do not create an untyped failed/rejected Promise
state. Cancellation is different from a domain error and does not require every
error enum to add a `Cancelled` variant. A plain `Promise<T>` is appropriate
only when normal completion is infallible; recoverable producer failures require
a Result (or another explicit sum type) inside `T`.

When `await` observes `Completed(value)`, it produces `value`. When it observes
`Pending`, it suspends and registers the continuation. When it observes
`Cancelled`, it cancels the Promise of the enclosing async function, runs that
frame's required cleanup, and executes no later statement in the frame. It does
not synthesize `T`, convert cancellation to `Err(E)`, or throw an exception.
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

Tests reject unqualified enum variants (`Green` instead of `Color.Green`), unknown enum members, and using the enum type itself as a value.

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

Procedure variables and procedure parameters are validated by tests. Function literals can appear in supported typed contexts, including call arguments where the receiving type is known.

Unsupported/rejected procedure cases include:

- closure captures in current procedure literal emission
- nested closure forms that require unsupported capture behavior
- procedure values at extern ABI boundaries
- procedure values inside unsupported list/array/generic emission paths
- direct inline invocation of an anonymous function literal

### `v0.6` closure design

Closures are part of language completion because async jobs, callbacks, and
resource-safe composition require behavior plus captured state. Function
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

The target semantics are:

- free lexical bindings referenced by the literal are captured automatically;
- immutable bindings are captured by value;
- mutable bindings are captured through a VM-owned rooted cell, so mutations
  are visible to the defining scope and every closure sharing that binding;
- an escaping closure extends the lifetime of its environment and mutable
  cells; it never retains a raw pointer into an expired stack frame;
- nested closures may capture an outer closure's environment;
- closure environments participate in precise GC tracing and deterministic
  cleanup of owned resources;
- closure values are not implicitly comparable, serializable, or valid at an
  extern/FFI boundary;
- host worker threads do not execute closures or access their environments
  directly; async closure execution resumes on VM-owned scheduler state.

A closure's callable type remains `fn ReturnType (...)`. Capture layout is an
implementation detail recorded in TAST/SIR/SBC metadata, not part of source type
identity. Two literals with the same `fn` signature are callable through that
signature but retain distinct environments.

Closure completion requires tests for immutable and mutable capture, escaping
lifetimes, nested environments, captures in generic specializations, GC during
calls/suspension, async callbacks, cancellation, resource cleanup, and
interpreter/JIT parity.

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
module Examples.Reference

import System.FFI

extern ffi.simple_add_i32 : i32 (a : i32, b : i32)

lib :: i64 = System.FFI.open("tests/ffi/libsimpleffi.so", ffi)

main :: i32 () {
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

- the planned `async` return modifier, `await` expression, and `?` propagation operator
- language-level `Result<T, E>`, `Option<T>`, and `Promise<T>` execution semantics
- generic methods and fully qualified/module-owned specialization
- executable canonical tagged generic `Result`, `Option`, and `Promise` values
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
