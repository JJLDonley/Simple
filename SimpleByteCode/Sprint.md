# Sprint Log - Simple VM

## 2026-01-29
- Created SBC design docs in `SimpleByteCode/` (headers, encoding, sections, metadata, opcodes, rules, debug, runtime).
- Implemented initial VM skeleton in `SimpleByteCode/vm/`.
- Added loader for SBC header/sections/tables.
- Added basic verifier (instruction boundary + simple stack checks).
- Added interpreter core with minimal opcode support (i32/bool, locals, calls, control flow).
- Added `build.sh` using g++ for local build.
- Added VM README.
- Added implementation plan `SimpleByteCode/Implementation.md`.

## Notes
- This log must record every VM-related change going forward.
