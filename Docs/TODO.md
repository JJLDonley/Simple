# Simple TODO

This list tracks work needed to improve feature independence, compiler structure, runtime safety, and tooling. It intentionally excludes `i128`/`u128` implementation work.

## Lang Architecture

- [ ] Replace facade-only Lang split with real implementation modules:
  - [ ] `Lexer`
  - [ ] `CAST`
  - [ ] `AST`
  - [ ] `RAST`
  - [ ] `TAST`
  - [ ] `IRB`
  - [ ] `IRE`
- [x] Move lexer implementation into `Lang/src/Lexer`.
- [x] Move parser implementation into `Lang/src/CAST`.
- [ ] Add real `CAST -> AST` normalization pass.
  - [x] Add initial `CAST -> AST` lowering boundary.
- [ ] Extract name/import/symbol resolution into `RAST`.
  - [x] Add initial RAST symbol collection boundary.
  - [x] Add callable parameter/local scope symbol collection.
- [ ] Extract type checking into `TAST`.
  - [x] Add initial TAST type-checker boundary.
- [ ] Replace direct SIR string emission with `IRB -> IRE`.
  - [x] Add initial `TAST -> IRB -> IRE` bridge.
- [ ] Update `CMakeLists.txt` as source files move into phase directories.
- [ ] Define deprecation/migration policy for legacy includes:
  - [ ] `lang_ast.h`
  - [ ] `lang_parser.h`
  - [ ] `lang_validate.h`
  - [ ] `lang_sir.h`

## Validator / Semantic Analysis

- [ ] Split `lang_validate.cpp` into focused modules:
  - [ ] resolver
  - [ ] type checker
  - [ ] control-flow checker
  - [ ] mutability checker
  - [ ] ABI checker
  - [ ] generics checker/substitution
- [ ] Add a canonical semantic context object carrying:
  - [ ] current function
  - [ ] current artifact
  - [ ] expected return type
  - [ ] expected expression type
  - [ ] loop depth
  - [ ] scope stack
- [ ] Replace repeated string-based lookup with resolved IDs:
  - [ ] `SymbolId`
  - [ ] `TypeId`
  - [ ] `FunctionId`
  - [ ] `ArtifactId`
  - [ ] `ModuleId`
  - [ ] `FieldId`
  - [ ] `EnumId`
  - [ ] `ExternId`
- [ ] Move contextual literal typing into TAST.
  - [x] Add initial TAST literal typing helpers.
- [ ] Persist expression types, mutability facts, and ABI facts on typed nodes.
- [x] Create canonical control-flow result:
  - [x] `may_fallthrough`
  - [x] `always_returns`
  - [x] `may_break`
  - [x] `may_skip`
- [x] Use the canonical control-flow result in both validation and lowering.

## AST / Feature Independence

- [x] Normalize top-level script body into an explicit AST node.
- [x] Normalize function literal declaration forms.
- [x] Normalize loop shorthand.
- [x] Normalize `|>` if-chain or define why it remains distinct.
- [x] Normalize switch branch forms.
- [x] Normalize call/member/index shapes.
- [x] Separate switch branch result markers from normal function `return` semantics.
- [ ] Formalize switch semantics:
  - [ ] statement switch
  - [ ] expression switch
  - [ ] assigning switch
  - [ ] block branch result rules
  - [ ] early function return inside branch blocks
  - [ ] `break`/`skip` behavior when nested in loops
  - [ ] no-fallthrough policy
- [x] Add resolved receiver model for artifact methods.
  - [x] Add initial RAST artifact member receiver refs from local variable types.
  - [x] Persist artifact receiver type and symbol on RAST member refs.
- [x] Disambiguate member access during resolution:
  - [x] Add initial RAST member reference collection for static and `self` member refs.
  - [x] module member
  - [x] artifact field
  - [x] artifact method
  - [x] enum member
  - [x] reserved module function
  - [x] extern symbol
  - [x] DL manifest call

## Procedure Values / Generics / Pointers

- [ ] Harden procedure values with tests across:
  - [ ] artifact methods
  - [ ] module functions
  - [ ] switch expressions
  - [ ] nested closures
  - [ ] lists/arrays
  - [ ] extern boundaries
  - [ ] generics
  - [ ] member calls
- [ ] Audit closure/upvalue semantics.
- [ ] Finish or explicitly reject unsupported generic cases:
  - [ ] generic functions
  - [ ] generic artifacts
  - [ ] generic methods
  - [ ] type-argument inference
  - [ ] specialization naming/mangling
  - [ ] duplicate specialization handling
- [ ] Finish or explicitly reject unsupported pointer cases:
  - [ ] pointer storage model
  - [ ] pointer mutability
  - [ ] pointer to artifact/list/array/string
  - [ ] dereference rules
  - [ ] pointer assignment
  - [ ] pointer ABI behavior
  - [ ] null pointer semantics
  - [ ] pointer safety diagnostics

## IR / SIR / Lowering

- [ ] Introduce structured language IR:
  - [ ] `IrModule`
  - [ ] `IrFunction`
  - [ ] `IrBlock`
  - [ ] `IrInst`
  - [ ] `IrType`
  - [ ] `IrSig`
  - [ ] `IrImport`
- [ ] Move artifact layout out of SIR emitter.
- [ ] Move ABI flattening out of SIR emitter.
- [ ] Move stack tracking into IRB.
- [ ] Move local/global/import/signature allocation into IRB.
- [ ] Make IRE serialize already-computed IR only.
- [ ] Keep SIR output stable while replacing internals.
- [ ] Add typed metadata builders instead of raw section byte buffers.

## Bytecode / Verifier / VM Coupling

- [ ] Centralize opcode semantics:
  - [ ] operand width
  - [ ] stack pops/pushes
  - [ ] type rules
  - [ ] control-flow behavior
  - [ ] verifier rule
  - [ ] VM dispatch mapping
- [ ] Add tests comparing opcode metadata, verifier behavior, and VM stack behavior.
- [ ] Reduce verifier/VM opcode semantic drift.
- [ ] Freeze or version opcode metadata used by external SBC producers.

## VM / Runtime

- [ ] Add typed heap layout helpers for:
  - [ ] string
  - [ ] array
  - [ ] list
  - [ ] artifact
  - [ ] closure
- [ ] Harden GC root tracing with stress tests for:
  - [ ] nested lists of strings
  - [ ] arrays of artifacts
  - [ ] closures capturing refs
  - [ ] artifact fields containing refs
  - [ ] switch/loop local lifetimes with refs
  - [ ] globals holding refs
  - [ ] temporary stack refs during calls
- [ ] Define and enforce runtime limits:
  - [ ] max stack
  - [ ] max locals
  - [ ] max call depth
  - [ ] max heap objects/bytes
  - [ ] max array/list size
  - [ ] max const pool size
  - [ ] max code size
- [ ] Keep interpreter as canonical until JIT behavior is fully specified.

## DL / FFI

- [ ] Write exact ABI mapping table.
- [ ] Document artifact layout rules.
- [ ] Document enum representation.
- [ ] Document string ownership/lifetime rules.
- [ ] Document pointer ownership rules.
- [ ] Add unsupported-shape diagnostics.
- [ ] Document and test Windows behavior or explicit lack of support.
- [ ] Move ABI checking/lowering into dedicated passes.

## CLI / Diagnostics / Imports

- [ ] Freeze CLI exit-code contract.
- [ ] Freeze stderr diagnostic format.
- [ ] Define `simple` vs `simplevm` behavior precisely.
- [ ] Document build output behavior.
- [ ] Document executable embedding/linking behavior.
- [ ] Document dynamic/static flags.
- [ ] Document missing file/import errors.
- [ ] Add stable diagnostic code ranges:
  - [ ] lexer: `E1xxx`
  - [ ] parser: `E2xxx`
  - [ ] resolver: `E3xxx`
  - [ ] type checker: `E4xxx`
  - [ ] IR/lowering: `E5xxx`
  - [ ] bytecode/verifier: `E6xxx`
  - [ ] runtime: `E7xxx`
  - [ ] CLI/imports: `E8xxx`
- [ ] Move import graph construction out of CLI into Lang/RAST or a shared module.
- [ ] Make CLI, LSP, and tests use the same import graph logic.

## Parser / Error Recovery / LSP

- [ ] Add systematic parser recovery after bad declarations.
- [ ] Improve parser recovery inside blocks.
- [ ] Support multiple diagnostics per file.
- [ ] Produce LSP-friendly partial ASTs.
- [ ] Keep stable spans for incomplete code.
- [ ] Integrate LSP with compiler phases:
  - [ ] CAST parse diagnostics
  - [ ] TAST semantic diagnostics
  - [ ] RAST go-to-definition
  - [ ] scope-based completion
  - [ ] resolved semantic tokens
  - [ ] symbol-ID rename

## Tests

- [ ] Split large test files by compiler phase:
  - [ ] `test_lexer.cpp`
  - [ ] `test_cast.cpp`
  - [ ] `test_ast.cpp`
  - [ ] `test_rast.cpp`
  - [ ] `test_tast.cpp`
  - [ ] `test_irb.cpp`
  - [ ] `test_ire.cpp`
  - [ ] `test_lang_integration.cpp`
- [ ] Add combinatorial feature-independence matrix tests:
  - [ ] artifact method -> switch -> if-chain -> loop -> list
  - [ ] module function -> fn literal -> switch -> artifact init
  - [ ] generic method -> array/list -> member call
  - [ ] extern call -> artifact literal -> nested artifact
- [ ] Add positive and negative tests for each nested feature combination.
- [ ] Add fixture tests for all unsupported-but-diagnosed feature combinations.

## Docs / Compatibility

- [ ] Add per-feature implementation status table:
  - [ ] parse
  - [ ] validate
  - [ ] emit
  - [ ] verify
  - [ ] runtime
  - [ ] tests
  - [ ] notes
- [ ] Freeze or explicitly version:
  - [ ] Lang syntax version
  - [ ] SIR version
  - [ ] SBC version
  - [ ] runtime ABI version
  - [ ] stdlib module version
- [ ] Decide whether `Docs/Sprint.md` keeps historical obsolete commands as-is or gains an obsolete-command note.
