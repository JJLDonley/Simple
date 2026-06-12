# Simple Language Architecture Plan

This document tracks the planned split of the monolithic language front-end into independent compiler stages.

## Goal

The current `Lang` implementation works, but major responsibilities are concentrated in a few files:

- `lang_parser.cpp`
- `lang_validate.cpp`
- `lang_sir.cpp`

The target architecture makes each phase explicit and independently testable:

```txt
source
  -> Lexer
  -> CAST
  -> AST
  -> RAST
  -> TAST
  -> IRB
  -> IRE
```

Existing public APIs must remain compatible during migration:

- `ParseProgramFromString`
- `ValidateProgramFromString`
- `EmitSirFromString`
- `EmitSir`

## Implemented

### Current Working Pipeline

The implemented compiler pipeline is currently:

```txt
source text
  -> lexer tokens
  -> legacy AST from parser
  -> validator over legacy AST
  -> direct SIR text emission
```

### Phase-0 Module Boundaries

The repository now has canonical include boundaries for the future split:

```txt
Lang/include/Lexer/token.h
Lang/include/Lexer/lexer.h
Lang/include/CAST/cast.h
Lang/include/CAST/parser.h
Lang/include/AST/ast.h
Lang/include/RAST/rast.h
Lang/include/TAST/tast.h
Lang/include/IRB/ir_builder.h
Lang/include/IRE/sir_emitter.h
```

Compatibility headers remain:

```txt
Lang/include/lang_token.h
Lang/include/lang_lexer.h
Lang/include/lang_ast.h
Lang/include/lang_parser.h
Lang/include/lang_validate.h
Lang/include/lang_sir.h
```

At phase 0, CAST/AST/RAST/TAST are facade boundaries over the existing legacy AST. This makes new code include the target modules without forcing a risky big-bang rewrite.

## Target Stages

### Lexer

Owns:

- token kinds
- source spans
- comments
- literals
- escape validation
- lexical diagnostics

Must not own:

- parsing rules
- type knowledge
- symbol resolution
- SIR emission

Target files:

```txt
Lang/include/Lexer/token.h
Lang/include/Lexer/lexer.h
Lang/src/Lexer/lexer.cpp
```

### CAST — Concrete AST

Parser-shaped tree. Preserves source syntax closely.

Owns:

- parsed declaration/statement/expression forms
- original syntactic shape
- token/source spans
- parser recovery information

Must not own:

- desugaring decisions
- symbol binding
- type checking
- ABI rules
- IR/SIR details

Target files:

```txt
Lang/include/CAST/cast.h
Lang/include/CAST/parser.h
Lang/src/CAST/parser.cpp
```

### AST — Normalized AST

Canonical surface language tree after parser cleanup.

Owns:

- normalized declarations
- normalized statement/expression forms
- desugared shorthand forms where safe
- stable tree shape for semantic phases

Examples of normalization candidates:

- loop shorthand normalization
- function literal binding normalization
- syntactic member/call/index cleanup
- top-level script body representation

Target files:

```txt
Lang/include/AST/ast.h
Lang/include/AST/lower_cast.h
Lang/src/AST/lower_cast.cpp
```

### RAST — Resolved AST

Name/scope/import resolved tree.

Owns:

- symbol tables
- import graph results
- scope resolution
- local/global/function/module/artifact/enum binding
- `self` binding
- extern manifest binding
- member lookup results

Must not own:

- final expression typing
- codegen indices
- bytecode/SIR strings

Target files:

```txt
Lang/include/RAST/rast.h
Lang/include/RAST/resolver.h
Lang/include/RAST/symbol_table.h
Lang/src/RAST/resolver.cpp
```

### TAST — Typed AST

Typed and validated semantic tree.

Owns:

- expression types
- contextual literal coercions
- mutability facts
- assignment validity
- call argument validation
- return-path validation
- generic substitution results for supported paths
- extern ABI validation results

Must not own:

- SIR text formatting
- opcode selection details where avoidable
- raw bytecode layout

Target files:

```txt
Lang/include/TAST/tast.h
Lang/include/TAST/type_checker.h
Lang/include/TAST/types.h
Lang/include/TAST/abi_checker.h
Lang/src/TAST/type_checker.cpp
Lang/src/TAST/abi_checker.cpp
```

### IRB — Language IR Builder

Structured IR construction layer between typed semantics and textual SIR.

Owns:

- language-level IR module object
- functions
- locals/globals/import metadata
- labels/basic blocks
- stack accounting
- type/field/signature metadata before serialization

Must not own:

- parsing
- name resolution
- type checking
- textual output formatting

Target files:

```txt
Lang/include/IRB/ir_builder.h
Lang/include/IRB/ir_module.h
Lang/src/IRB/ir_builder.cpp
Lang/src/IRB/tast_to_ir.cpp
```

### IRE — IR Emitter

Serializes language IR.

Initial output:

```txt
IRB module -> SIR text
```

Possible future output:

```txt
IRB module -> Simple::IR::IrModule or SBC directly
```

Owns:

- SIR text formatting
- deterministic section ordering
- stable emitted names
- serialization diagnostics

Target files:

```txt
Lang/include/IRE/sir_emitter.h
Lang/src/IRE/sir_emitter.cpp
```

## Migration Plan

### Phase 0 — Facades and Documentation

Status: implemented.

- Add this plan.
- Add canonical include directories for all target stages.
- Move lexer/token public declarations behind `Lexer/` headers.
- Keep legacy compatibility headers.
- Add tests proving new include boundaries compile and preserve behavior.

### Phase 1 — Lexer Ownership

Status: in progress.

Implemented:
- Moved `lang_lexer.cpp` to `Lang/src/Lexer/lexer.cpp`.
- Updated CMake source paths.
- Kept `lang_lexer.h` and `lang_token.h` as compatibility includes.
- Added new include-path coverage through language phase tests.

Remaining:
- Move token names and lexer API fully to `Lexer/` without legacy dependencies.
- Add dedicated lexer test file when tests are split by phase.

### Phase 2 — CAST Parser Split

Status: in progress.

Implemented:
- Moved parser implementation to `Lang/src/CAST/parser.cpp`.
- Updated CMake source paths.
- Kept `lang_parser.h` as a compatibility include path.
- Added parser coverage through the `CAST/parser.h` include path.

Remaining:
- Move parser node ownership to `CAST`.
- Make parser return `CAST::Program` as the canonical type.
- Keep legacy aliases while tests migrate.
- Separate parser recovery data from semantic AST nodes.

### Phase 3 — AST Normalization

- Add `CAST -> AST` lowering pass.
- Move desugaring/normalization out of parser and validator.
- Make validator consume normalized `AST::Program`.

### Phase 4 — RAST Resolver

- Extract symbol/import/member resolution from `lang_validate.cpp`.
- Add explicit symbol table data structures.
- Make later phases use resolved symbol ids instead of repeated string lookup.

### Phase 5 — TAST Type Checker

- Extract type checking from `lang_validate.cpp`.
- Persist expression types and mutability facts on typed nodes.
- Remove repeated type inference from SIR emission.

### Phase 6 — IRB Structured Builder

- Replace direct string emission in `lang_sir.cpp` with structured IR construction.
- Track labels, locals, globals, imports, signatures, artifact layouts, and stack use in IRB objects.

### Phase 7 — IRE SIR Emitter

- Move SIR text serialization into IRE.
- Keep byte-for-byte stable output where possible.
- Make `EmitSir` a facade over `TAST -> IRB -> IRE`.

### Phase 8 — Cleanup

- Retire legacy direct dependencies after downstream users migrate.
- Shrink compatibility headers to deprecation shims.
- Split tests by compiler phase.

## Testing Requirements

Each phase must include tests that prove:

- legacy public APIs still work
- new stage headers compile
- old fixtures still pass
- failures stay deterministic
- no stage reaches backward into later-stage responsibilities

Minimum smoke tests per phase:

```txt
Lexer: tokenization and invalid character/escape diagnostics
CAST: parser tree shape and parser recovery
AST: normalization equivalence
RAST: symbol/import/member resolution
TAST: type/mutability/ABI checks
IRB: structured IR shape and stack accounting
IRE: SIR output compiles and runs
```

## Non-Goals During Split

- no language feature expansion solely for the split
- no bytecode compatibility change unless explicitly required
- no VM behavior changes
- no removal of legacy public APIs until migration is complete
