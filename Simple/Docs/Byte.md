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

## Legacy Specs

Historical SBC design references live in `Simple/Docs/legacy/`:

- `SBC_Headers.md`
- `SBC_Encoding.md`
- `SBC_Sections.md`
- `SBC_Metadata_Tables.md`
- `SBC_OpCodes.md`
- `SBC_Rules.md`
