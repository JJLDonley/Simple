# Simple documentation

This directory contains the public documentation for the Simple language and
its implementation.

## Language and libraries

- [Language reference](Language.md) — syntax, types, declarations, modules,
  imports, and FFI.
- [System library](System.md) — low-level runtime and host-facing modules.
- [Standard library](Standard.md) — higher-level library modules.

## Toolchain

- [Command-line interface](CLI.md) — `svm` commands and behavior.
- [Simple IR](IR.md) — the readable compiler intermediate representation.
- [Simple Bytecode](Byte.md) — the serialized bytecode format and verifier.
- [Virtual machine](VM.md) — execution, memory, native calls, and runtime
  behavior.
- [LLVM JIT](JIT.md) — optional LLVM 18 execution support.

## Development

- [Portability](Portability.md) — operating-system and architecture boundary.
- [Coding standards](Standards.md) — conventions for compiler and runtime
  changes.

Release planning, implementation audits, and personal working notes are not
part of the public documentation. Keep those in the ignored `.notes/`
directory rather than adding them under `docs/`.
