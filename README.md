# Simple

Simple is a strict, statically typed programming language that compiles `.simple` source to SIR, lowers SIR to SBC bytecode, and executes it on the Simple VM.

The project includes the language front-end, IR/SIR compiler layer, SBC bytecode loader/verifier, VM runtime, optional JIT scaffolding, CLI, tests, and editor/LSP code.

## Quick links

- [Language reference](Docs/Language.md)
- [VM runtime](Docs/VM.md)
- [IR / SIR](Docs/IR.md)
- [SBC bytecode](Docs/Byte.md)
- [CLI](Docs/CLI.md)
- [JIT](Docs/JIT.md)
- [TODO](Docs/TODO.md)
- [Project standards](Docs/Standards.md)

## Example

```simple
import IO

Point :: Artifact {
  x : i32
  y : i32

  sum : i32 () {
    return self.x + self.y
  }
}

main : i32 () {
  p : Point = { .x = 3, .y = 4 }
  IO.println("sum={}", p.sum())
  return p.sum()
}
```

Key syntax:

- `name : Type` declares a mutable binding.
- `name :: Type` declares an immutable binding or a top-level kind declaration.
- `package Name` declares a file/package header for import indexing.
- `Thing :: Artifact`, `Thing :: Module`, and `Thing :: Enum` define language objects.
- `skip` is loop-continue.
- Primitive casts use `@Type(value)`, for example `@i32(x)`.
- `System.*` is the canonical standard-library namespace; compatibility imports such as `IO`, `Math`, `FS`, and `DL` are supported.

## Build

```bash
./build.sh
```

On Windows:

```bat
build.bat
```

The root `bin/` directory is intentionally kept small:

```txt
bin/svm      # compiler/tooling/runtime CLI
bin/simple   # runtime stub only; not a compiler
```

The compiler CMake target is still named `simplevm`; the runtime-stub target is `simple_stub`.

## Run programs

```bash
./bin/svm run Tests/simple/hello.simple
./bin/svm check Tests/simple/point_sum.simple
./bin/svm emit Tests/simple/hello.simple
```

The CLI supports `.simple`, `.sir`, and `.sbc` workflows. See [Docs/CLI.md](Docs/CLI.md).

## Test

```bash
cmake --build build --target simplevm_tests -j2
./build/bin/simplevm_tests
```

The test suite covers parser/validator behavior, IR/SBC emission, bytecode loading and verification, VM execution, native runtime modules, CLI behavior, LSP behavior, and JIT tiering/scaffolding.

## Source map

Use this as the quick source for finding implementation code:

| Area | Path | Doc |
|---|---|---|
| Language front-end | `Lang/` | [Docs/Language.md](Docs/Language.md) |
| IR / SIR compiler | `IR/` | [Docs/IR.md](Docs/IR.md) |
| SBC bytecode | `Byte/` | [Docs/Byte.md](Docs/Byte.md) |
| VM runtime | `VM/` | [Docs/VM.md](Docs/VM.md) |
| JIT scaffolding | `VM/include/jit/`, `VM/src/jit/` | [Docs/JIT.md](Docs/JIT.md) |
| CLI | `CLI/` | [Docs/CLI.md](Docs/CLI.md) |
| LSP/editor support | `LSP/`, `Editor/` | CLI-integrated; no separate active doc |
| Tests | `Tests/` | See command above |

## Current status

Implemented today:

- `.simple` parsing and validation
- SIR emission and IR/SBC lowering
- SBC loader/verifier
- VM interpreter and runtime imports
- heap objects for strings, arrays, lists, artifacts, closures, and runtime payloads
- reserved/System standard-library modules
- dynamic-library interop on supported platforms
- CLI workflows for run/check/build/compile/emit/lsp
- optional JIT tiering and compiled-runner scaffolding

Known limits are documented in the relevant subsystem docs, especially [Docs/Language.md](Docs/Language.md), [Docs/VM.md](Docs/VM.md), and [Docs/JIT.md](Docs/JIT.md).

## Documentation policy

The active docs are intentionally small:

```txt
Docs/TODO.md
Docs/Language.md
Docs/VM.md
Docs/IR.md
Docs/Byte.md
Docs/CLI.md
Docs/JIT.md
Docs/Standards.md
```

If behavior changes, update the matching doc in the same change.
