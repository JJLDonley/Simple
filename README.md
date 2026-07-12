# Simple

<p align="center">
  <strong>Simple like C. Statically typed. General purpose.</strong>
</p>

<p align="center">
  <a href="Docs/Language.md">Language</a> ·
  <a href="Docs/Timeline.md">Timeline</a> ·
  <a href="Docs/IR.md">SIR</a> ·
  <a href="Docs/Byte.md">SBC</a> ·
  <a href="Docs/VM.md">VM</a> ·
  <a href="https://github.com/JJLDonley/Simple/releases">Releases</a>
</p>

Simple: identity-first, strictly typed — scripted in minutes, portable everywhere.

It compiles `.simple` source to portable SBC bytecode for the Simple VM, with JIT, GC, canonical System and Standard libraries, and explicit FFI.

The project includes the language front-end, resolver, type checker, SIR/IR lowering, SBC loader/verifier, interpreter runtime, native modules, CLI, LSP server, and JIT scaffolding.

> Status: active development. Simple is usable for fixtures, tests, imports, artifacts, arrays/lists, native modules, and dynamic-library experiments. Syntax and APIs are still evolving while the native-library architecture settles.

---

## Contents

- [Why Simple](#why-simple)
- [Install / build](#install--build)
- [Getting started](#getting-started)
- [Language taste](#language-taste)
- [Toolchain](#toolchain)
- [Runtime](#runtime)
- [Library direction](#library-direction)
- [CLI](#cli)
- [Tests](#tests)
- [Repository map](#repository-map)
- [Documentation](#documentation)

---

## Why Simple

Simple came about from wanting an identity-first, strictly typed language that scripts quickly and ships as portable VM bytecode.

Simple keeps data, functions, namespaces, artifacts, modules, and native APIs as direct language tools. Use the shape that fits the program.

| Goal | What it means |
|---|---|
| **General purpose** | Build tools, scripts, applications, native-backed programs, and VM-hosted code. |
| **Static typing** | Variables, parameters, returns, arrays, lists, artifacts, enums, and native signatures are checked before execution. |
| **Quick scripting** | Top-level statements let small programs stay small. |
| **Readable pipeline** | `Language → SIR → SBC → VM` is inspectable. |
| **Native library focus** | Low-level `System.*` and high-level `Standard.*` APIs are the current roadmap priority. |

---

## Install / build

### Release build matrix

Simple publishes one release version with explicit runtime flavors. The flavor is an artifact
classifier, not a separate SemVer version.

| Operating system | Architecture | `int` | `llvm` | Package |
|---|---|:---:|:---:|---|
| Linux | x86_64 | ✅ | ✅ | `.tar.gz` |
| macOS | runner architecture (`arm64` or `x86_64`) | ✅ | ✅ | `.tar.gz` |
| Windows | x86_64 | ✅ | 🧪 Build/test only | `.zip` (`int` only) |

- `int` is the dependency-light interpreter build.
- `llvm` includes the LLVM ORC JIT and falls back to the interpreter for unsupported functions.
- Windows LLVM 18 is built and tested as an allowed-to-fail experimental CI job, but is not
  published and cannot block the stable packages.
- Windows LLVM packaging is deferred until its LLVM runtime and toolchain distribution are stable.
- Every published flavor runs the full test suite during release CI.

Artifact names follow this form:

```txt
simple-v0.5.0-linux-x86_64-int.tar.gz
simple-v0.5.0-linux-x86_64-llvm.tar.gz
simple-v0.5.0-darwin-arm64-int.tar.gz
simple-v0.5.0-darwin-arm64-llvm.tar.gz
simple-v0.5.0-windows-x86_64-int.zip
```

`simple-latest-<platform>-<architecture>-<flavor>` aliases are also published.

### Download a release

Download a package from the
[latest GitHub release](https://github.com/JJLDonley/Simple/releases/latest) before considering a
source build. Replace `int` with `llvm` on Linux or macOS when JIT support is wanted.

#### Linux x86_64

```bash
curl -LO https://github.com/JJLDonley/Simple/releases/latest/download/simple-latest-linux-x86_64-int.tar.gz
tar -xzf simple-latest-linux-x86_64-int.tar.gz
cd simple-*-linux-x86_64
./bin/svm version
```

#### macOS

Apple Silicon:

```bash
curl -LO https://github.com/JJLDonley/Simple/releases/latest/download/simple-latest-darwin-arm64-int.tar.gz
tar -xzf simple-latest-darwin-arm64-int.tar.gz
cd simple-*-darwin-arm64
./bin/svm version
```

Intel macOS uses `darwin-x86_64` in the file name instead.

#### Windows x86_64

PowerShell:

```powershell
Invoke-WebRequest `
  https://github.com/JJLDonley/Simple/releases/latest/download/simple-latest-windows-x86_64-int.zip `
  -OutFile simple-int.zip
Expand-Archive simple-int.zip -DestinationPath simple-int
& .\simple-int\bin\svm.exe version
```

The extracted directory can be moved anywhere. Add its `bin` directory to `PATH` to invoke `svm`
without a full path.

### Source-build prerequisites

| Requirement | Linux | macOS | Windows |
|---|---|---|---|
| C++ toolchain | GCC or Clang | Xcode Command Line Tools | Visual Studio 2022 C++ tools |
| Build system | CMake | CMake | CMake |
| FFI | `libffi-dev`, `pkg-config` | Homebrew `libffi`, `pkg-config` | vcpkg `libffi:x64-windows` |
| LLVM flavor | `llvm-18-dev` | Homebrew `llvm@18` | Not currently packaged |
| Shell | Bash | Bash | Git Bash/MSYS2 for release scripts |

Typical dependency installation:

```bash
# Ubuntu/Debian, interpreter build
sudo apt-get install cmake build-essential libffi-dev pkg-config

# Ubuntu/Debian, LLVM build
sudo apt-get install cmake build-essential libffi-dev pkg-config llvm-18-dev

# macOS
xcode-select --install
brew install cmake libffi pkg-config        # interpreter
brew install llvm@18                        # additionally for LLVM JIT
```

### Build from source

Interpreter build on Linux/macOS:

```bash
./build.sh
```

LLVM JIT build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIMPLEVM_ENABLE_LLVM_JIT=ON
cmake --build build --target simplevm simple_stub --parallel 2
```

On Homebrew systems, CMake may need the LLVM package location:

```bash
cmake -S . -B build -DSIMPLEVM_ENABLE_LLVM_JIT=ON \
  -DLLVM_DIR="$(brew --prefix llvm@18)/lib/cmake/llvm"
```

Windows interpreter build:

```bat
build.bat
```

Build output:

```txt
bin/svm      compiler/tooling/runtime CLI
bin/simple   runtime stub only; not a compiler
```

Build internals, tests, static libraries, and shared libraries live under `build/bin/`.

### Install

Linux/macOS:

```bash
./install.sh
```

Custom prefix:

```bash
PREFIX="$HOME/.local" ./install.sh
```

Windows:

```bat
install.bat
```

Custom Windows install directory:

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

---

## Getting started

Create `hello.simple`:

```simple
import Standard.IO

main : i32 () {
  Standard.IO.println("hello from Simple")
  return 0
}
```

Run it:

```bash
svm run hello.simple
```

Check without running:

```bash
svm check hello.simple
```

Emit bytecode:

```bash
svm emit -sbc hello.simple --out hello.sbc
```

Top-level statements are also valid for script-style programs:

```simple
import Standard.IO

Standard.IO.println("script start")

add : i32 (a : i32, b : i32) {
  return a + b
}

Standard.IO.println("sum={}", add(2, 3))
```

---

## Language taste

### Declarations and mutability

`:` declares mutable bindings. `::` declares immutable bindings or named top-level constructs.

```simple
count : i32 = 1
limit :: i32 = 10

count = count + 1
```

### Functions

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}

log : void (message : string) {
  Standard.IO.println(message)
}
```

### Artifacts

Artifacts define structured data with optional methods. Fields define layout. Methods define behavior.

```simple
Counter :: artifact {
  value : i32

  inc : void () {
    self.value = self.value + 1
  }

  get : i32 () {
    return self.value
  }
}

main : i32 () {
  c : Counter = { .value = 0 }
  c.inc()
  return c.get()
}
```

### Namespaces

Namespaces group related declarations.

```simple
Maths :: namespace {
  PI :: f64 = 3.14159

  add : i32 (a : i32, b : i32) {
    return a + b
  }
}

main : i32 () {
  return Maths.add(40, 2)
}
```

### Enums

Enum members are scoped under the enum type.

```simple
Color :: enum {
  Red = 1
  Green = 2
  Blue = 3
}

favorite : Color = Color.Green
```

### Arrays and lists

Fixed arrays use `T{N}`. Growable lists use `T[]`.

```simple
fixed : i32{3} = {1, 2, 3}
items : i32[] = [1, 2, 3]

items[1] = 9
count : i32 = len(items)
```

### Casts

Primitive casts use `@Type(value)`:

```simple
a : i8 = 40
b : i8 = 2
sum : i32 = @i32(a) + @i32(b)
```

### Modules and imports

`module` names a file/module for import indexing. Named imports are resolved through the project/module index.

```simple
module Tools.Math

add : i32 (a : i32, b : i32) {
  return a + b
}
```

Use it from another file:

```simple
import Tools.Math

main : i32 () {
  return add(40, 2)
}
```

### FFI and extern

Native interop uses strict `extern` declarations. Unsupported ABI shapes are rejected instead of guessed.

```simple
import System.FFI
import Standard.IO

extern raylib.InitWindow : void (w : i32, h : i32, title : string)
extern raylib.CloseWindow : void ()

lib : i64 = System.FFI.open("libraylib.so", raylib)
if (lib == 0) {
  Standard.IO.println("raylib load failed: {}", System.FFI.lastError())
  return 1
}

raylib.InitWindow(800, 600, "Simple")
raylib.CloseWindow()
```

---

## Toolchain

```txt
Language → SIR → SBC → VM
```

| Layer | Role |
|---|---|
| **Language** | `.simple` source, parser, resolver, type checker. |
| **SIR** | Readable intermediate representation for inspecting compiler lowering. |
| **SBC** | Compact bytecode artifact with loader/verifier checks. |
| **VM** | Verified execution, heap, native dispatch, traps, limits. |

Common inspection commands:

```bash
svm emit -ir main.simple --out main.sir
svm emit -sbc main.simple --out main.sbc
svm run main.simple
```

---

## Runtime

The Simple runtime loads SBC bytecode, verifies structure, executes instructions, manages VM-owned values, and provides native-backed runtime modules.

| Runtime area | Responsibility |
|---|---|
| Loader | Reads SBC headers, sections, bytecode version, and entry metadata. |
| Verifier | Rejects unsupported or malformed bytecode before execution. |
| Interpreter | Executes instructions, call frames, locals, globals, traps, and runtime limits. |
| Heap / GC | Owns strings, arrays, lists, artifacts, and runtime objects. |
| Native dispatch | Connects runtime calls to native modules through checked boundaries. |

Expected runtime/OS failures should become values such as `Result<T>` or `Option<T>` as the native library evolves. Traps remain for invalid contracts, stale/wrong handles, bytecode violations, and VM invariants.

---

## Library direction

Simple’s native library roadmap is layered:

| Layer | Role |
|---|---|
| VM Native Core | Resource registry, handle validation, metadata, cleanup, capability policy. |
| `System.*` | Low-level APIs close to host/runtime behavior. |
| `Standard.*` | High-level APIs built over `System.*`. |
| Legacy diagnostics | Short imports are rejected with canonical `System.*` / `Standard.*` replacements; they are not aliases. |

### Core native-facing types

| Type | Purpose |
|---|---|
| `System.Handle<T>` | Typed opaque native resource handle. |
| `Result<T>` | Expected success/failure result. |
| `Option<T>` | Presence/absence or ready/not-ready value. |
| `Promise<T>` | Planned async result carrier. |

### Planned `System.*` modules

| Module | Purpose |
|---|---|
| `System.FS` | Files, directories, file handles, read/write/seek/stat/list/copy/remove. |
| `System.Path` | Join, normalize, absolute/relative paths, basename, dirname, extension. |
| `System.Buffer` | Native buffer handles, slicing, copying, bounds-checked access. |
| `System.Bytes` | Heap-owned byte sequences and byte/string conversion. |
| `System.Net` | TCP/UDP sockets, listeners, connect/listen/accept/send/receive/close. |
| `System.HTTP` | HTTP clients, server handles, request/response IO, limits, timeouts. |
| `Standard.HTTPS` | TLS-backed clients/servers, cert loading, verification diagnostics over `System.HTTP`/`System.Net`. |
| `System.Terminal` | Raw mode, alternate screen, cursor, style, key/mouse/resize events. |
| `System.Process` | Spawn, wait, kill, stdio, capability checks. |
| `System.Thread` | OS thread handles and thread-related runtime operations. |
| `System.Job` | VM jobs, spawn/join/detach/cancel, result propagation. |
| `System.Channel` | Send/receive/try operations, pending, close, wakeup behavior. |
| `System.Time` | Wall/monotonic clocks, timers, sleep, durations. |
| `System.Random` | Seeded random generators and random numeric values. |
| `System.Json` | Parse, stringify, typed accessors, JSON value handles. |
| `System.Log` | Levels, structured messages, runtime/file sinks. |
| `System.FFI` | Foreign function interface, dynamic libraries, symbols, strict ABI. |
| `System.ASM` | C/DynASM native-code units, object generation, stub/AOT linking. |

---

## CLI

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

`simple` is the runtime-stub executable name used for embedded SBC payloads.

---

## Tests

```bash
cmake --build build --target simplevm_tests -j2
./build/bin/simplevm_tests
```

---

## Repository map

| Area | Path | Doc |
|---|---|---|
| Language front-end | `Lang/` | [Docs/Language.md](Docs/Language.md) |
| IR / SIR compiler | `IR/` | [Docs/IR.md](Docs/IR.md) |
| SBC bytecode | `Byte/` | [Docs/Byte.md](Docs/Byte.md) |
| VM runtime | `VM/` | [Docs/VM.md](Docs/VM.md) |
| JIT scaffolding | `VM/include/jit/`, `VM/src/jit/` | [Docs/JIT.md](Docs/JIT.md) |
| CLI and build stubs | `CLI/` | [Docs/CLI.md](Docs/CLI.md) |
| LSP/editor support | `LSP/`, `Editor/` | `svm lsp` |
| Tests and fixtures | `Tests/` | [Tests](#tests) |

---

## Documentation

| Doc | Contents |
|---|---|
| [Docs/Language.md](Docs/Language.md) | Syntax, semantics, imports, artifacts, enums, FFI. |
| [Docs/VM.md](Docs/VM.md) | Runtime, heap, imports, ABI, execution model. |
| [Docs/IR.md](Docs/IR.md) | SIR/IR contract. |
| [Docs/Byte.md](Docs/Byte.md) | SBC bytecode, loader, verifier. |
| [Docs/CLI.md](Docs/CLI.md) | `svm` and `simple` behavior. |
| [Docs/JIT.md](Docs/JIT.md) | Optional JIT/tiering behavior. |
| [Docs/Timeline.md](Docs/Timeline.md) | Native-library roadmap and lower-priority backlog. |
| [Docs/Standards.md](Docs/Standards.md) | Project coding standards. |

---

## Version

Current tool version: `v0.5.0`.

The canonical tool/package version lives in `VERSION`. Build scripts and release CI read that file, and CMake embeds the same value into `svm version`.
