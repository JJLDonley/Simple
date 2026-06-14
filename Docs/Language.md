# Simple Language

Simple is a strict, statically typed language that compiles `.simple` source to SIR and then to SBC bytecode.

This document describes the language as exercised by the current parser, validator, fixtures, and tests.

## Compilation pipeline

```txt
.simple source
  -> tokens
  -> parsed program
  -> normalized AST
  -> resolved names/imports
  -> typed program
  -> SIR text
  -> SBC bytecode
```

Compatibility entry points include `ParseProgramFromString`, `ValidateProgramFromString`, `ValidateProgramFromStringDiagnostic`, `EmitSirFromString`, and `EmitSir`.

## Program structure

A source file contains imports plus declarations and/or script statements.

```simple
import System.io

main : i32 () {
  System.io.println("hello")
  return 0
}
```

Top-level executable statements are collected into an implicit script entry. If there are no top-level script statements and `main` exists, `main` is used as entry. Top-level `return` is invalid.

## Declarations and mutability

Variables and functions use name-first syntax.

Mutable declarations use `:`:

```simple
x : i32 = 1
x = x + 1
```

Immutable declarations use `::`:

```simple
answer :: i32 = 42
```

Assigning to an immutable local, parameter, field, module variable, return value, or immutable pointer base is rejected.

Functions use the same marker before the return type:

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}
```

Parameters also use `:` for mutable and `::` for immutable parameter bindings.

## Types

Primitive types accepted by the type parser include:

- `i8`, `i16`, `i32`, `i64`, `i128`
- `u8`, `u16`, `u32`, `u64`, `u128`
- `f32`, `f64`
- `bool`, `char`, `string`, `void`

Composite type forms include:

```simple
i32{10}      // fixed array size syntax
i32[]        // list syntax
i32[][]      // nested lists
Map<string, i32>
i32*
void**
fn bool (i32, string)
fn i32 ()
```

Not every parsed type is fully supported by every runtime/ABI path; unsupported runtime paths are rejected later by validation or VM checks.

## Casts

Primitive casts require the `@` form:

```simple
a : i8 = 1
b : i8 = 2
return add(@i32(a), @i32(b))
```

Using primitive type names as normal calls for casts is rejected; tests expect diagnostics such as “primitive cast syntax requires '@'”.

String conversions are also expressed with `@string(value)` where supported by validation/runtime tests.

## Expressions

Implemented expression forms include:

- identifiers and literals
- unary and binary operators
- calls and generic calls
- member access with `.` and pointer member access with `->`
- indexing
- array/list literals
- artifact literals with named fields
- function literals
- switch expressions
- format strings

Assignment operators include `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, and `>>=`. Increment/decrement are validated against mutability.

## Statements and control flow

Supported statements include:

- variable declarations
- expression statements
- assignment statements
- `return`
- `if` / `else`
- `while`
- `for` range forms
- `break`
- `skip`
- switch expression blocks

`skip` is the loop-continue statement.

## Artifacts

Artifacts are declared with `:: artifact`:

```simple
Point :: artifact {
  x : i32
  y : i32

  sum : i32 () {
    return self.x + self.y
  }
}
```

Artifact member access inside methods uses `self`. Tests reject unqualified artifact field access when `self` is required.

Artifacts support named initialization where covered by fixtures/tests.

## Modules, imports, and using

Modules are declared with `:: module`. Imports bring modules/files into scope. `System.*` is the canonical standard-library namespace.

Reserved compatibility imports include names such as `Math`, `IO`, `Time`, `File`, `DL`, `OS`, `FS`, `Log`, `Buffer`, `Json`, and `Channel`; these map to runtime modules documented in `Docs/StdLib.md`.

`using` is supported for imported/reserved modules where tests cover it.

## Enums and switch

Enums are declared with `:: enum`. Switch expressions use `=>` branches and a `default` branch where needed:

```simple
return switch (value) {
  value > 0 => { tmp : i32 = 1; return tmp }
  default => return 0
}
```

Validation checks branch shapes and result compatibility.

## Procedure values and function literals

Procedure types use `fn`:

```simple
fn i32 (i32, i32)
```

Typed function literals are supported where the parser/type checker can infer or receive the procedure type. Direct inline invocation of an anonymous function literal remains unsupported.

## Extern and dynamic-library metadata

`extern` declarations describe ABI contracts for host or dynamic-library calls. Validation checks declared shapes before runtime. The VM rejects unsupported ABI shapes such as recursive artifact ABI for DL calls.

## Diagnostics

Language diagnostics carry a code, phase, source span, message, and optional help text. CLI and LSP render the same diagnostics for terminal and editor clients.
