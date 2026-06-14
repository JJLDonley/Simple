# Simple Language

This is the canonical language-front-end document. It combines the previous language contract, phase ownership, and migration notes into one current reference.

## Scope

`Simple::Lang` owns `.simple` source handling from text through SIR emission:

```txt
source -> Lexer -> CAST -> AST -> RAST -> TAST -> IRB -> IRE -> SIR text
```

The implementation is strict and statically typed. Unsupported constructs are rejected before SIR emission.

## Owned files

- Lexer: `Lang/include/Lexer/`, `Lang/src/Lexer/`
- CAST parser tree: `Lang/include/CAST/`, `Lang/src/CAST/`
- Normalized AST: `Lang/include/AST/`, `Lang/src/AST/`
- Resolver/imports/symbols: `Lang/include/RAST/`, `Lang/src/RAST/`
- Type checking/control-flow/mutability/ABI: `Lang/include/TAST/`, `Lang/src/TAST/`
- IR builder: `Lang/include/IRB/`, `Lang/src/IRB/`
- SIR emitter: `Lang/include/IRE/`, `Lang/src/IRE/`
- Compatibility headers: `Lang/include/lang_*.h`
- Structured diagnostics: `Lang/include/Diagnostics/`, `Lang/src/Diagnostics/`

## Public API

Preferred phase APIs:

- `Simple::Lang::CAST::ParseProgramFromString`
- `Simple::Lang::AST::LowerCastProgram`
- `Simple::Lang::RAST::ResolveProgram`
- `Simple::Lang::TAST::CheckResolvedProgram`
- `Simple::Lang::IRB::BuildModule`
- `Simple::Lang::IRE::EmitSirModule`

Compatibility APIs remain available during the v1 window:

- `ParseProgramFromString`
- `ValidateProgramFromString`
- `ValidateProgramFromStringDiagnostic`
- `EmitSirFromString`
- `EmitSir`

New compiler code should prefer phase headers. Legacy `lang_*.h` headers are compatibility surfaces, not the place for new phase logic.

## Forbidden dependencies

- Lexer and CAST must not depend on semantic phases.
- RAST owns names, imports, symbols, and member references only.
- TAST owns types, mutability, ABI facts, and control-flow type facts.
- IRB consumes TAST, not raw parser state.
- IRE consumes IRB modules.
- CLI and LSP must not duplicate compiler semantic checks.

## Program entry

- Top-level declarations are declarations only.
- Top-level statements are collected into an implicit script entry function.
- If no top-level script statements exist and `main` exists, `main` is used as the entry.
- Top-level `return` is rejected.

Supported `main` form:

```simple
main : i32 () {
  return 0
}
```

## Syntax and semantics

Implemented language surface includes:

- identifiers, literals, calls, member access, indexing, unary and binary operators
- primitive scalar types: signed/unsigned integers, floats, bool, char, void
- strings, arrays, lists, artifacts, enums, references/pointers, and procedure values
- `let`, `mut`, assignment, blocks, `if`, loops, `break`, `continue`, `return`
- switch expressions with enforced default/exhaustiveness rules used by tests
- procedure declarations and typed function literals
- imports, modules, `using`, reserved standard-library imports, and extern declarations
- explicit casts using `@T(value)`
- string and IO format placeholder validation

Mutability is explicit: immutable bindings cannot be reassigned, mutable bindings use `mut`, and pointer/address-of validation preserves mutable/immutable access rules.

## Modules and imports

- `import Name` binds a module namespace.
- Reserved imports map to runtime namespaces documented in `Docs/StdLib.md`.
- `System.*` names are canonical for standard-library modules.
- Import graph, path resolution, and symbol indexing live in RAST.

## Extern and ABI metadata

`extern` declarations describe host/dynamic-library ABI contracts. The language validates shape and metadata before the VM attempts runtime binding. Recursive artifact ABI for extern/DL is rejected by the runtime.

## Diagnostics

Compiler phases produce structured diagnostics through `Simple::Lang::Diagnostics`. CLI renders terminal messages; LSP converts diagnostics to protocol JSON.

## Tests

Language coverage lives in:

- `Tests/tests/test_lang.cpp`
- `Tests/tests/lang/test_*.cpp`
- `.simple` fixtures under `Tests/simple/` and related fixture directories

Guard tests enforce phase boundaries, structured diagnostics, import behavior, ABI validation, and current syntax/semantic contracts.
