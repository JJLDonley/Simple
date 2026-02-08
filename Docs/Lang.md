# Simple::Lang (Authoritative)

This document is authoritative for the Simple language front-end design and current supported semantics.

## Scope

`Simple::Lang` owns:

- tokenization/parsing of `.simple` source
- AST/type representation
- semantic validation and diagnostics
- lowering to SIR text consumed by `Simple::IR`

## Compiler Architecture

### 1) Lexer

- token stream generation with line/column tracking
- literal/operator/keyword classification
- error production for malformed tokens and unterminated constructs

### 2) Parser

- top-level declarations: import/extern/function/variable/artifact/module/enum
- statement and expression parsing with precedence
- type parsing including arrays/lists/procedures/generics/pointers

### 3) AST

Core shape is defined in `Lang/include/lang_ast.h`:

- `TypeRef` (name, pointer depth, generics, dims, proc type)
- `Expr` variants (literal/call/member/index/array/list/artifact/fn)
- declarations (`VarDecl`, `FuncDecl`, `ArtifactDecl`, `ModuleDecl`, `EnumDecl`, `ExternDecl`)

### 4) Validator

Implemented in `Lang/src/lang_validate.cpp`:

- symbol/scope checks
- mutability enforcement (`:` vs `::`)
- expression type compatibility
- control-flow checks (return paths/conditions)
- import/extern/module contract checks
- dynamic DL manifest signature checks

### 5) SIR Emitter

Implemented in `Lang/src/lang_sir.cpp`:

- emits types/sigs/consts/globals/imports/functions sections
- lowers control flow and typed operations to SIR opcodes
- lowers extern calls/imports and `Core.*` reserved modules
- emits global init function flow when needed

## Language Design Contracts

### Mutability Model

- `:` mutable binding/member/return
- `::` immutable binding/member/return
- immutable assignment attempts are compile errors

### Type Model (Current)

Supported primitives:

- signed ints: `i8`, `i16`, `i32`, `i64`, `i128`
- unsigned ints: `u8`, `u16`, `u32`, `u64`, `u128`
- floats: `f32`, `f64`
- misc: `bool`, `char`, `string`

Composite/supporting types:

- arrays (`T[N]`)
- lists (`T[]`)
- procedure types
- user types (`artifact`, `enum`, `module`)
- pointers (`*T`, `*void`) for FFI boundaries

### Cast Syntax

- Primitive casts use `@T(value)` syntax.
- Example: `x : i32 = @i32(raw_i8)`.
- Bare call-style casts (`i32(value)`) are rejected with an explicit diagnostic directing `@T(value)`.
- Cast validation/emission remains strict and type-checked (for example unsupported source/target combinations still fail).

### IO Formatting

- `IO.print` / `IO.println` support both:
  - single-value form: `IO.println(value)`
  - format form: `IO.println("x={}, y={}", x, y)`
- Format form rules:
  - first argument must be a string literal,
  - `{}` placeholder count must match provided value arguments,
  - value arguments must be scalar printable types (`numeric`, `bool`, `char`, `string`).

### Reserved Imports / Stdlib Surface

Reserved paths (`Math`, `IO`, `Time`, `File`, `Core.DL`, `Core.Os`, `Core.Fs`, `Core.Log`) are compiler-mapped to core runtime modules; see `Docs/StdLib.md`.

`System.*` import aliases are also accepted (case-insensitive) and map to the same runtime modules:

- `System.io` / `System.stream` -> `IO`
- `System.math` -> `Math`
- `System.time` -> `Time`
- `System.file` / `System.fs` -> `Core.Fs`
- `System.dl` -> `Core.DL`
- `System.os` -> `Core.Os`
- `System.log` -> `Core.Log`

Import declarations accept both quoted and unquoted module paths:

- `import "Core.DL" as DL`
- `import System.dl as DL`

Reserved imports also register an implicit lowercase leaf alias when no explicit `as` alias is provided:

- `import system.io` enables `io.println(...)`
- `import System.DL` enables `dl.open(...)`

Legacy direct `IO.print/IO.println` calls remain supported.

### Project-Local Imports (CLI)

For `.simple` entry files compiled through CLI commands (`run/check/build/compile/emit -ir/-sbc`):

- non-reserved imports are resolved relative to the importing file directory,
- extensionless imports are allowed (`import "./raylib"` resolves `./raylib.simple`),
- imports are loaded recursively,
- cyclic local imports are rejected.

## Program Entry / Script Semantics

- A `.simple` program may run without defining `main`.
- If top-level statements exist, the compiler synthesizes an implicit entry (`__script_entry`) and executes those statements in source order.
- Top-level function declarations are not auto-invoked.
- Top-level `return` is disallowed and reported as a validation error.
- If no top-level statements exist, normal function entry selection applies (`main` when present).

## FFI + Extern Design

### Extern Syntax

Authoritative extern declaration shape:

- `extern ffi.symbol : RetType (args...)`
- declaration-only (no body)

### Dynamic DL Manifest Flow

`DL.Open(path, manifest_module)` uses extern declarations under that module as call signatures.

Current support:

- exact primitive lanes
- pointers
- enums
- artifacts by-value (runtime struct marshalling)

Explicit limitation:

- recursive struct ABI unsupported

## Global Initialization Design

- globals are emitted with init constants and optional generated `__global_init`
- globals with init expressions are initialized through generated init call before main logic
- ref-like fallback init values are normalized to VM null-ref semantics at runtime

## Diagnostics Contract

- errors include stable message text and location context when available
- unsupported constructs fail explicitly at validation/emission stage
- no silent type coercion across unsupported language surfaces

## Supported Surface (Alpha Snapshot)

Implemented and expected to be stable:

- functions, variables, control-flow, expressions
- arrays/lists/artifacts/enums/modules
- imports/externs and reserved core modules
- pointer syntax for FFI boundaries
- typed dynamic DL call path

Deferred/not fully finalized surfaces are treated as compile errors, not partial runtime behavior.

## Primary Files

- `Lang/include/lang_ast.h`
- `Lang/src/lang_lexer.cpp`
- `Lang/src/lang_parser.cpp`
- `Lang/src/lang_validate.cpp`
- `Lang/src/lang_sir.cpp`

## Verification Commands

- `./build.sh --suite lang`
- `./build.sh --suite all`
