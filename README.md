# Simple

Simple is a statically typed systems-style programming language that compiles to a compact bytecode format and runs on the Simple VM.

The project is both a language implementation and a VM/toolchain playground: it includes the parser, resolver, type checker, SIR/IR lowering, SBC bytecode loader/verifier, interpreter runtime, native standard-library bindings, CLI, LSP server, and JIT scaffolding.

> Status: active development. The language is feature-rich enough for fixtures, runtime tests, imports, native modules, artifacts, lists, arrays, and dynamic-library experiments, but the docs and APIs should still be treated as evolving.

## Contents

- [Why Simple?](#why-simple)
- [Build from source](#build-from-source)
- [Install](#install)
- [Tests](#tests)
- [Hello world](#hello-world)
- [Language taste](#language-taste)
- [CLI overview](#cli-overview)
- [Build outputs](#build-outputs)
- [Compiler pipeline](#compiler-pipeline)
- [Repository map](#repository-map)
- [Documentation](#documentation)

## Why Simple?

Simple is designed around a few practical goals:

- **Readable typed syntax** with explicit declarations and direct control flow.
- **Clear compilation stages** from source to IR to bytecode.
- **A verified bytecode runtime** with defensive loading and verification.
- **Native runtime modules** for real programs: IO, FS, paths, env, JSON, buffers, channels, random, time, logging, dynamic loading, and more.
- **Embeddable execution** through generated runtime stubs.
- **Tooling-first development** with a CLI, LSP server, and a large test suite.

## Build from source

Linux/macOS:

```bash
./build.sh
```

Windows:

```bat
build.bat
```

The scripts build the compiler/runtime tools and place the developer binaries in `bin/`:

```txt
bin/svm      compiler/tooling/runtime CLI
bin/simple   runtime stub only; not a compiler
```

Build internals, tests, static libraries, and shared libraries stay under `build/bin/`.

## Install

Install `svm` and `simple` onto your PATH after building.

Linux/macOS default install location is `/usr/local/bin`:

```bash
./install.sh
```

Override the prefix if desired:

```bash
PREFIX="$HOME/.local" ./install.sh
```

Windows default install location is `%LocalAppData%\Simple\bin`:

```bat
install.bat
```

Override it with:

```bat
set SIMPLE_INSTALL_DIR=C:\Tools\Simple\bin
install.bat
```

After install, use `svm` directly:

```bash
svm run Tests/simple/hello.simple
svm check Tests/simple/point_sum.simple
svm emit -sbc Tests/simple/hello.simple --out hello.sbc
```

`simple` is reserved for generated/embedded runtime stubs and is not a compiler command.

## Tests

```bash
cmake --build build --target simplevm_tests -j2
./build/bin/simplevm_tests
```

## Hello world

```simple
import IO

main : i32 () {
  IO.println("hello from Simple")
  return 0
}
```

Run it:

```bash
svm run hello.simple
```

## Language taste

### Declarations and mutability

```simple
main : i32 () {
  count : i32 = 1      // mutable
  limit :: i32 = 10    // immutable

  count += 1
  return count + limit
}
```

### File/package headers

`package` names a file for import indexing. It does not create a runtime namespace.

```simple
package Tools.Math

add : i32 (a : i32, b : i32) {
  return a + b
}
```

Import it from another file:

```simple
import Tools.Math

main : i32 () {
  return add(40, 2)
}
```

### Artifacts

Artifacts are record-like types with fields and methods.

```simple
Point :: Artifact {
  x : i32
  y : i32

  sum : i32 () {
    return self.x + self.y
  }
}

main : i32 () {
  p : Point = { .x = 3, .y = 4 }
  return p.sum()
}
```

### Modules

Modules are language namespace objects. They are separate from `package` headers.

```simple
Math :: Module {
  two :: i32 = 2

  add : i32 (a : i32, b : i32) {
    return a + b
  }
}

main : i32 () {
  return Math.add(Math.two, 40)
}
```

### Lists, arrays, loops

```simple
main : i32 () {
  values : i32[] = [1, 2, 3]
  values.push(4)

  total : i32 = 0
  for (i : i32 = 0; i < values.len(); i += 1) {
    total += values[i]
  }

  return total
}
```

### Casts

Primitive casts use `@Type(value)`:

```simple
a : i8 = 40
b : i8 = 2
return @i32(a) + @i32(b)
```

### Standard library imports

`System.*` is canonical. Compatibility imports such as `IO`, `Math`, `FS`, `DL`, `Json`, `Buffer`, and `Channel` are also supported.

```simple
import FS
import Json

main : i32 () {
  text : string = FS.readText("data.json")
  handle : i64 = Json.parse(text)
  Json.free(handle)
  return 0
}
```

See [Docs/Language.md](Docs/Language.md) for the full language reference.

## CLI overview

`svm` is the compiler/tooling/runtime command:

```bash
svm run <file.simple|file.sir|module.sbc>
svm check <file.simple|file.sir|module.sbc>
svm build <file.simple|file.sir> --out <program|program.sbc>
svm compile <file.simple|file.sir> --out <program|program.sbc>
svm emit -ir <file.simple> --out <file.sir>
svm emit -sbc <file.simple|file.sir> --out <file.sbc>
svm lsp
```

`simple` is not a compiler. It is the runtime-stub executable name used for embedded SBC payloads.

## Build outputs

The root `bin/` directory is intentionally small:

```txt
bin/svm      compiler/tooling/runtime CLI
bin/simple   runtime stub only; not a compiler
```

Build internals, tests, static libraries, and shared libraries live under `build/bin/`.

## Compiler pipeline

```txt
.simple source
  -> Lang lexer/parser
  -> AST / RAST / TAST
  -> SIR text
  -> IR lowering
  -> SBC bytecode
  -> Byte loader/verifier
  -> VM interpreter or optional JIT path
```

The interpreter is the correctness baseline. The JIT is optional scaffolding for tiering and compiled-runner experiments.

## Repository map

| Area | Path | Doc |
|---|---|---|
| Language front-end | `Lang/` | [Docs/Language.md](Docs/Language.md) |
| IR / SIR compiler | `IR/` | [Docs/IR.md](Docs/IR.md) |
| SBC bytecode | `Byte/` | [Docs/Byte.md](Docs/Byte.md) |
| VM runtime | `VM/` | [Docs/VM.md](Docs/VM.md) |
| JIT scaffolding | `VM/include/jit/`, `VM/src/jit/` | [Docs/JIT.md](Docs/JIT.md) |
| CLI and build stubs | `CLI/` | [Docs/CLI.md](Docs/CLI.md) |
| LSP/editor support | `LSP/`, `Editor/` | via `svm lsp` |
| Tests and fixtures | `Tests/` | see [Tests](#tests) |

## Documentation

Active docs are intentionally small and focused:

- [Docs/Language.md](Docs/Language.md) — language syntax, semantics, imports, stdlib surface
- [Docs/VM.md](Docs/VM.md) — runtime, heap, imports, ABI, execution model
- [Docs/IR.md](Docs/IR.md) — SIR/IR contract
- [Docs/Byte.md](Docs/Byte.md) — SBC bytecode, loader, verifier
- [Docs/CLI.md](Docs/CLI.md) — `svm` and `simple` behavior
- [Docs/JIT.md](Docs/JIT.md) — optional JIT/tiering behavior
- [Docs/TODO.md](Docs/TODO.md) — active work list
- [Docs/Standards.md](Docs/Standards.md) — coding standards

