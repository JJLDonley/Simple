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
- Removed committed VM binary `SimpleByteCode/vm/bin/simplevm`.
- Expanded loader validation (section IDs, bounds, table index checks).
- Expanded verifier checks for locals/globals and call signature validation.
- Added VM test harness and initial bytecode test (CONST_I32 + ADD_I32).
- Updated build script to compile test binary.
- Added `SimpleByteCode/vm/.gitignore` to prevent committing built binaries.
- Implemented global storage in VM with LOAD_GLOBAL/STORE_GLOBAL support.
- Added global read/write test to the VM test harness.

## Notes
- This log must record every VM-related change going forward.
