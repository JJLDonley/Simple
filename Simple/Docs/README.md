# Simple Docs Index

This directory contains the authoritative documentation for the Simple VM stack.

## Bytecode + VM
- `SBC_Headers.md`: SBC file header layout.
- `SBC_Encoding.md`: binary encoding rules.
- `SBC_Sections.md`: section table layout and constraints.
- `SBC_Metadata_Tables.md`: metadata tables and row formats.
- `SBC_OpCodes.md`: opcode list and operands.
- `SBC_Rules.md`: loader + verifier rules.
- `SBC_Runtime.md`: runtime model and execution semantics.
- `SBC_Debug.md`: debug tables and line mapping.
- `SBC_ABI.md`: ABI/FFI surface (v0.1 target).

## IR
- `SBC_IR.md`: SIR (Simple IR) text format and lowering rules.
- `Modules.md`: module-by-module overview and plans.

## Module Docs (Simple::)
- `VM.md`
- `Byte.md`
- `IR.md`
- `CLI.md`
- `Lang.md`

## Legacy References
The `SBC_*` and `Simple_*` documents are legacy references. Their content is merged into the
module docs above.

## Project Planning
- `Implementation.md`: module-based phase plan.
- `Sprint.md`: chronological change log.

## Legacy / Reference
- `Simple_VM_Opcode_Spec.md`
- `Simple_Programming_Language_Document.md`
- `Simple_Implementation_Document.md`

Notes:
- `SBC_ABI.md` is the freeze target for ABI/FFI when v0.1 locks.
