# Simple::Byte

## Scope

`Simple::Byte` owns the SBC binary format contract, loader, and verifier.

## Current State

Implemented and actively used in all execution paths:

- SBC header and section parsing.
- Table/heap decoding with strict bounds checks.
- Verifier pass for stack, type, and control-flow safety.
- Validation of function signatures, jump targets, and global init metadata.

## Alpha Contract (Intended)

- Loader rejects malformed binaries deterministically.
- Verifier is required for safe execution mode.
- SBC compatibility changes require version bump and explicit migration.

## Authoritative Format Contract

These are the active enforced expectations for v0.1 modules:

- Header magic must match `SBC0` (`0x30434253`).
- Header version must match current loader-supported version.
- Section ranges must be in bounds and non-overlapping.
- Table row sizes/counts must match section metadata.
- Signature parameter ranges must stay in `param_types` bounds.
- Type rows must pass kind/size/field-shape consistency checks.

## Authoritative Verifier Contract

- Stack underflow/overflow paths are rejected.
- Control-flow targets must land on valid instruction boundaries.
- Call/call-indirect/tailcall arity/type checks must match signature metadata.
- Return type must match declared function signature.
- Invalid local/global/type/signature indices are rejected.

## Known Limits / Explicit Constraints

- Unknown/unsupported header values are hard-rejected (no soft compatibility mode).
- Unsupported type/signature/opcode combinations fail verification instead of degrading.

## Primary Files

- `Simple/Byte/include/sbc_types.h`
- `Simple/Byte/src/sbc_loader.cpp`
- `Simple/Byte/src/sbc_verifier.cpp`

## Verification Commands

- `./Simple/build.sh --suite core`
- `./Simple/build.sh --suite all`

## Legacy Migration Notes

Historical design references live in `Simple/Docs/legacy/` and are non-authoritative:

- `SBC_Headers.md`
- `SBC_Encoding.md`
- `SBC_Sections.md`
- `SBC_Metadata_Tables.md`
- `SBC_OpCodes.md`
- `SBC_Rules.md`
