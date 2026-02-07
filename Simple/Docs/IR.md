# Simple::IR

## Scope

`Simple::IR` owns SIR text parsing/validation and lowering to SBC.

## Current State

Implemented and used by language + tests:

- SIR text parser and tokenizer.
- Metadata tables (`types`, `sigs`, `consts`, `imports`, `globals`).
- Name resolution and type validation.
- Lowering to SBC sections and bytecode.
- Line-aware diagnostics.

## Alpha Contract (Intended)

- SIR is a stable compiler-facing intermediate target for this project phase.
- Invalid SIR fails with deterministic diagnostics.
- Lowered SBC must pass loader/verifier contracts.

## Authoritative SIR Contract

- SIR text must resolve all referenced tables/symbols before lowering.
- SIR signatures/globals/imports must emit valid SBC metadata indices.
- Unsupported SIR constructs fail in compiler diagnostics (no silent coercion).
- Lowered output is expected to pass `Simple::Byte` loader + verifier.

## Known Limits / Explicit Constraints

- SIR supports project-defined lowering subset; unsupported constructs fail at compile-time.
- `i128/u128` and other advanced const/type surfaces have explicit constraints in lowering paths.

## Primary Files

- `Simple/IR/src/ir_lang.cpp`
- `Simple/IR/src/ir_builder.cpp`
- `Simple/IR/include/ir_lang.h`

## Verification Commands

- `./Simple/build.sh --suite ir`
- `./Simple/build.sh --suite all`

## Legacy Migration Notes

Historical IR reference (non-authoritative):

- `Simple/Docs/legacy/SBC_IR.md`
