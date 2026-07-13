# Simple

Simple is a statically typed, general-purpose language built around a portable
bytecode runtime. Source code is checked ahead of execution, lowered to Simple
IR (SIR), encoded as Simple Bytecode (SBC), and run by the Simple VM.

The interpreter is the reference runtime on Linux, macOS, and Windows. An
optional LLVM 18 ORC JIT accelerates supported bytecode without changing the
portable SBC distribution format.

Simple is under active development. It can compile and run the programs in its
test suite, but the language, libraries, bytecode format, and tooling are not
yet stable.

## Contents

- [Releases](#releases)
- [Development dependencies](#development-dependencies)
- [Install and build](#install-and-build)
- [Development expectations](#development-expectations)
- [The language](#the-language)
- [Command line](#command-line)
- [Compiler and runtime](#compiler-and-runtime)
- [Tests](#tests)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

## Releases

Packages are published on the
[GitHub releases page](https://github.com/JJLDonley/Simple/releases).

Simple uses the release channel encoded by its version:

- `v0.X.0` is a stable milestone release.
- `v0.X.Y`, where `Y` is nonzero, is an experimental development release toward the next stable milestone.

Experimental releases are marked as GitHub prereleases and do not replace the stable `latest` packages.

| Platform | Interpreter (`int`) | LLVM 18 (`llvm`) | Archive |
|---|:---:|:---:|---|
| Linux x86_64 | supported | supported | `.tar.gz` |
| macOS arm64/x86_64 | supported | supported | `.tar.gz` |
| Windows x86_64 | supported | experimental | `.zip` |

Use an `int` package for the interpreter-only runtime. Use an `llvm` package
to enable the LLVM ORC JIT; unsupported functions still run in the
interpreter. Windows LLVM support is experimental and may not be available for
every release.

Archive names include the version, platform, architecture, and runtime flavor:

```text
simple-v0.5.0-linux-x86_64-int.tar.gz
simple-v0.5.0-linux-x86_64-llvm.tar.gz
simple-v0.5.0-darwin-arm64-int.tar.gz
simple-v0.5.0-windows-x86_64-int.zip
```

Aliases beginning with `simple-latest-` point to the newest stable `v0.X.0` package for a given target and flavor. Experimental patch releases use only versioned archive names.

## Development dependencies

All source builds require:

- CMake 3.16 or newer;
- a C++17 compiler;
- libffi development headers and libraries;
- Git;
- Bash for the Unix build scripts, or Git Bash for Windows release scripts.

| Platform | Compiler | Interpreter dependencies | LLVM build |
|---|---|---|---|
| Ubuntu/Debian | GCC or Clang | `libffi-dev`, `pkg-config` | `llvm-18-dev` |
| macOS | Xcode command-line tools | Homebrew `libffi`, `pkg-config` | Homebrew `llvm@18` |
| Windows | Visual Studio C++ tools | vcpkg `libffi` | vcpkg LLVM 18 SDK |

Typical setup commands:

```sh
# Ubuntu/Debian interpreter build
sudo apt-get install cmake build-essential libffi-dev pkg-config

# Add this for LLVM support
sudo apt-get install llvm-18-dev
```

```sh
# macOS
xcode-select --install
brew install cmake libffi pkg-config

# Add this for LLVM support
brew install llvm@18
```

Windows development uses a Visual Studio developer environment. The vcpkg
manifests are under `cmake/platform/vcpkg/`, with the static release triplet in
`cmake/platform/triplets/`.

## Install and build

### Install a release

Download the package for your platform from the
[latest release](https://github.com/JJLDonley/Simple/releases/latest), extract
it, and add its `bin` directory to `PATH`.

Linux example:

```sh
curl -LO https://github.com/JJLDonley/Simple/releases/latest/download/simple-latest-linux-x86_64-int.tar.gz
tar -xzf simple-latest-linux-x86_64-int.tar.gz
./simple-*/bin/svm version
```

macOS uses `darwin-arm64` or `darwin-x86_64` in the archive name.

Windows PowerShell example:

```powershell
Invoke-WebRequest `
  https://github.com/JJLDonley/Simple/releases/latest/download/simple-latest-windows-x86_64-int.zip `
  -OutFile simple.zip
Expand-Archive simple.zip -DestinationPath simple
& .\simple\bin\svm.exe version
```

The extracted package can be moved without rebuilding it.

### Build from source

Interpreter build on Linux or macOS:

```sh
./scripts/build/local.sh
```

Manual CMake build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

LLVM build:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIMPLEVM_ENABLE_LLVM_JIT=ON
cmake --build build --parallel 2
```

Homebrew may require its LLVM package path explicitly:

```sh
cmake -S . -B build \
  -DSIMPLEVM_ENABLE_LLVM_JIT=ON \
  -DLLVM_DIR="$(brew --prefix llvm@18)/lib/cmake/llvm"
```

Windows interpreter build:

```bat
scripts\build\windows.bat
```

Build output is written under `build/bin/`. Platform scripts copy the
user-facing commands to `bin/`:

```text
bin/svm       compiler, runtime, and language server
bin/simple    embedded-program runtime stub
```

Install a local Unix build with LLVM 18 JIT enabled by default:

```sh
./scripts/install/unix.sh
```

Use `./scripts/install/unix.sh --no-llvm-jit` for an interpreter-only local install. If changing compilers causes CMake to regenerate its cache, the installer reapplies the requested JIT mode before building.

On Windows:

```bat
scripts\install\windows.bat
```

## Development expectations

The path to a stable language is divided into a few broad stages:

| Stage | Expected outcome |
|---|---|
| `0.5` | Establish the compiler, canonical `System.*`/`Standard.*` imports, interpreter, and optional JIT baseline. |
| `0.6` | Complete the native runtime foundations and make the core libraries consistent across supported operating systems. |
| `0.7` | Finish the intended core language surface and remove temporary language restrictions. |
| `0.8` | Harden the VM, verifier, GC, JIT, ABI, and performance behavior. |
| `0.9` | Stabilize projects, packages, editor tooling, documentation, and distribution. |
| `1.0` | Freeze the documented language, bytecode, runtime, and compatibility contracts. |

These are expectations rather than release promises. A stage is complete only
when its behavior is implemented, tested on supported platforms, documented,
and represented consistently in the compiler, runtime, CLI, and LSP.

## The language

Every Simple source file begins with a required `module` declaration. That name is the source unit's runtime module namespace and named-import identity. The file may then define a `main` function or use top-level statements for short scripts.

```simple
module examples.hello

import Standard.IO

main :: i32 () {
  Standard.IO.println("hello, world")
  return 0
}
```

Run it with:

```sh
svm run hello.simple
```

### Language contents

| Topic | Contents |
|---|---|
| [Lexical rules](docs/Language.md#lexical-rules) | Keywords, comments, operators, numeric literals, strings, and character escapes. |
| [Programs and modules](docs/Language.md#program-structure-and-entry-points) | Entry points, top-level statements, source files, and module headers. |
| [Declarations](docs/Language.md#declarations) | Mutable and immutable variables, functions, and `::` declarations. |
| [Types](docs/Language.md#types) | Primitive types, arrays, lists, generics, procedures, and pointers. |
| [Literals](docs/Language.md#literals) | Numeric, boolean, character, string, array, list, and artifact values. |
| [Expressions](docs/Language.md#expressions) | Operators, calls, indexing, member access, assignment, and casts. |
| [Control flow](docs/Language.md#statements-and-control-flow) | `if`, `else`, `while`, `for`, `switch`, and `return`. |
| [Functions](docs/Language.md#functions-procedure-types-and-function-literals) | Parameters, return values, procedure types, and function literals. |
| [Arrays and lists](docs/Language.md#arrays-and-lists) | Fixed-size arrays, growable lists, indexing, and list methods. |
| [Artifacts](docs/Language.md#artifacts) | Structured values, fields, methods, initialization, and `self`. |
| [Enums](docs/Language.md#enums) | Named integer-backed values and scoped members. |
| [Imports and `using`](docs/Language.md#imports-and-using) | Named project modules, explicit quoted source paths, aliases, and canonical library imports. |
| [System and Standard libraries](docs/Language.md#reservedsystem-modules-and-standard-library) | Runtime-facing `System.*` modules and higher-level `Standard.*` modules. |
| [Extern and FFI](docs/Language.md#extern-declarations-and-ffi-abi) | Native declarations, dynamic libraries, supported ABI types, and rejection rules. |
| [Diagnostics](docs/Language.md#diagnostics) | Compiler error classes and unsupported constructs. |
| [Known limitations](docs/Language.md#known-limitations) | Language features that are incomplete or deliberately restricted. |

### Bindings and functions

```simple
count : i32 = 0
limit :: i32 = 10

add :: i32 (a : i32, b : i32) {
  return a + b
}

count = add(count, 1)
```

### Artifacts

Artifacts combine structured data and methods:

```simple
module Examples.Reference

Counter :: artifact {
  value : i32

  increment :: void () {
    self.value += 1
  }
}

main :: i32 () {
  counter : Counter = { .value = 0 }
  counter.increment()
  return counter.value
}
```

### Arrays and lists

```simple
coordinates : i32{3} = {10, 20, 30}
values : i32[] = [1, 2, 3]
values[1] = 9
```

### Modules and imports

A module declaration is required for scripts and declaration-oriented source files. Imports accept declared module names as well as quoted source paths.

```simple
module Tools.Math

MathTools :: namespace {
  add :: i32 (a : i32, b : i32) {
    return a + b
  }
}
```

Another source unit can import its declared module name:

```simple
module App.Main

import Tools.Math

main :: i32 () {
  return MathTools.add(40, 2)
}
```

Explicit local source imports are also supported when path-based resolution is intended:

```simple
module App.Local

import "./local_helpers"
import "./legacy_helpers.simple"
```

The complete language reference is [docs/Language.md](docs/Language.md).

## Command line

```text
svm run <source.simple|module.sir|module.sbc>
svm check <source.simple|module.sir|module.sbc>
svm build <source.simple|module.sir> --out <program>
svm emit -ir <source.simple> --out <module.sir>
svm emit -sbc <source.simple|module.sir> --out <module.sbc>
svm lsp
svm version
```

Examples:

```sh
svm check hello.simple
svm emit -ir hello.simple --out hello.sir
svm emit -sbc hello.simple --out hello.sbc
svm run hello.sbc
svm build hello.simple --out hello
```

`svm build` uses the static runtime by default. `-d` requests a shared-runtime
development build.

## Compiler and runtime

```text
.simple source
      |
      v
 lexer / parser / resolver / type checker
      |
      v
 Simple IR (SIR)
      |
      v
 Simple Bytecode (SBC)
      |
      v
 verifier -> interpreter or LLVM JIT
```

SIR is a readable intermediate form used to inspect compiler lowering. SBC is
the serialized format consumed by the VM. The verifier rejects malformed or
unsupported bytecode before execution.

Host-specific behavior is isolated under `source/Platform/`. Portable compiler
and VM code use that interface rather than embedding operating-system checks.
See [docs/Portability.md](docs/Portability.md) for details.

## Tests

```sh
cmake -S . -B build
cmake --build build --target simplevm_tests --parallel 2
./build/bin/simplevm_tests
```

Windows:

```bat
build\bin\simplevm_tests.exe
```

The suite covers the language front end, IR, bytecode, VM, native modules, CLI,
LSP, and end-to-end language fixtures.

## Repository layout

```text
source/       compiler, VM, CLI, LSP, and platform code
tests/        unit tests and language fixtures
docs/         public language and implementation documentation
editor/       editor integrations
cmake/        CMake and platform configuration
scripts/      build, install, audit, and editor scripts
```

Generated files belong in `build/`, `bin/`, and `dist/`. Personal planning and
audit notes belong in the ignored `.notes/` directory.

## Documentation

Start with the [documentation index](docs/README.md).

- [Language reference](docs/Language.md)
- [System library](docs/System.md)
- [Standard library](docs/Standard.md)
- [Command-line interface](docs/CLI.md)
- [Simple IR](docs/IR.md)
- [Simple Bytecode](docs/Byte.md)
- [Virtual machine](docs/VM.md)
- [Jobs and promises](docs/Async.md)
- [LLVM JIT](docs/JIT.md)
- [Portability](docs/Portability.md)
- [Coding standards](docs/Standards.md)

## Contributing

Follow the mandatory [coding standards](docs/Standards.md): use modern C++17 ownership and type-safety practices, keep platform behavior behind the platform interface, and leave no shims or dead code. Run the continuous cleanup gate after every commit-sized update. Add tests for observable changes, update affected public documentation, and run the required full-suite/configuration checks before submitting code.

## License

Simple is distributed under the terms in [LICENSE](LICENSE).
