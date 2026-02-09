# Simple Documentation

Simple is a strictly typed language and runtime stack:

- Strictly typed language front-end (`.simple`)
- Strictly typed IR (`SIR`)
- Structured bytecode format (`SBC`)
- Strictly typed VM execution and verification
- Core standard library modules
- C/C++ interop focus through `DL` (working dynamic library flow)

This page is the entry point. Detailed behavior lives in module docs below.

## Pipeline

1. `Simple::Lang` parses + validates `.simple`
2. Emits `SIR` text
3. `Simple::IR` lowers `SIR` -> `SBC`
4. `Simple::Byte` loads + verifies `SBC`
5. `Simple::VM` executes verified modules

## What To Read

| Doc | Purpose |
|---|---|
| `Docs/Lang.md` | Language syntax, typing, declarations, control flow, artifacts, enums, imports |
| `Docs/StdLib.md` | Core library modules, reserved import keywords, `extern` + `DL` interop |
| `Docs/Byte.md` | SBC meaning, sections/tables, opcode families, verifier invariants |
| `Docs/IR.md` | SIR meaning, structure, lowering rules, examples |
| `Docs/VM.md` | VM runtime model, call frames, heap/GC, import dispatch, DLL ABI path |
| `Docs/CLI.md` | CLI commands and build/run/check/emit/lsp workflows |
| `Docs/Modules.md` | Ownership boundaries across Lang/IR/Byte/VM/CLI |
| `Docs/Sprint.md` | Change log and execution history |

## Purpose And Design Priorities

- Strict type contracts across language, IR, bytecode, and VM
- Explicit validation over implicit coercion
- Broad opcode surface with deterministic verification
- Native interop via stable `extern` manifests and `DL`
- Practical build and install experience across Linux/macOS/Windows

## High-Level Overview

This section is a fast overview for readers who do not want to read every module doc first.

### Runtime and Compiler Stack

- Strictly typed VM execution model
- Extensive opcode surface for control flow, arithmetic, calls, objects, arrays/lists, and interop
- Strictly typed IR (`SIR`) between language and bytecode
- Core runtime library modules (`IO`, `Math`, `Time`, `Fs`, `Os`, `DL`, `Log`)
- C/C++ interop focus with working dynamic library flow (`DL`)

### Bytecode (SBC): Meaning and Data

SBC is the executable bytecode format consumed by the VM.

#### SBC Binary Layout

| Part | Description |
|---|---|
| Header | Magic/version/endian + section metadata |
| Section table | Section ids, offsets, sizes |
| Metadata sections | Types, sigs, methods, functions, globals, imports, exports |
| Const pool | Encoded constant payloads (`string`, `i128/u128` blobs, etc.) |
| Code | Opcode stream with fixed-width operands |
| Debug (optional) | File/line/symbol metadata |

#### Core Section Meaning

| Section | What it stores |
|---|---|
| `types` | Type rows (`TypeKind`, size/layout metadata) |
| `sigs` | Function signatures (return + param type list) |
| `methods` | Method metadata (name/sig/local counts) |
| `functions` | Executable code ranges + stack metadata |
| `globals` | Global slots + optional init constants |
| `imports` | Runtime import bindings (module/symbol/sig) |
| `exports` | Publicly exported function bindings |
| `const_pool` | Constant bytes addressed by offsets |
| `code` | Bytecode instructions |

#### Opcode Families (`<T>` lanes)

`<T>` means a type-specialized variant (`i32`, `i64`, `f32`, `f64`, `ref`, etc.).

| Family | Pattern examples |
|---|---|
| Constants | `const_<T>`, `const_string`, `const_null` |
| Arithmetic | `add_<T>`, `sub_<T>`, `mul_<T>`, `div_<T>`, `mod_<T>` |
| Unary numeric | `neg_<T>`, `inc_<T>`, `dec_<T>` |
| Compare/bool | `cmp_<op>_<T>`, `bool_not`, `bool_and`, `bool_or` |
| Data movement | `load_local`, `store_local`, `load_global`, `store_global`, upvalue ops |
| Calls | `call`, `call_indirect`, `tailcall`, `ret`, `enter`, `leave` |
| Control flow | `jmp`, `jmp_true`, `jmp_false`, `jmp_table` |
| Objects/collections | object/field/ref ops, array/list typed lanes, string ops |
| Runtime hooks | `intrinsic`, `sys_call`, debug/profile ops |

#### SBC-Style Instruction Example

```asm
func add locals=2 stack=2 sig=sig_add
  enter 2
  load_local 0
  load_local 1
  add_i32
  ret
```

Loader + verifier reject invalid structure, bad ids, bad jump targets, and signature mismatches before VM execution.

### IR (SIR): Meaning and Data

SIR is the typed intermediate representation lowered into SBC.

#### SIR Section Model

| SIR section | Purpose |
|---|---|
| `types` | Declares type aliases/rows used by signatures and metadata |
| `sigs` | Declares callable signatures |
| `consts` | Declares constants referenced by id/name |
| `imports` | Declares external/runtime call bindings |
| `globals` | Declares global storage and optional init |
| `func` | Declares function body, locals, stack, signature |
| `entry` | Declares entry function |

#### SIR Example (Complete Minimal Module)

```asm
types:
  t_i32 = i32

sigs:
  sig_add = i32(i32, i32)

func add locals=2 stack=2 sig=sig_add
  enter 2
  load_local 0
  load_local 1
  add_i32
  ret

entry add
```

#### SIR Lowering Contract

| Rule | Result when violated |
|---|---|
| Every referenced name/type/sig exists | Lowering error |
| Opcode operand width matches opcode contract | Lowering error |
| Labels resolve and are unique within function | Lowering error |
| Call arity and param types match signature | Lowering/verifier error |
| Emitted ids/offsets are in range | Loader/verifier error |

SIR is strict by design: no implicit type fallback, no unresolved symbol guessing.

### Simple Language Overview

- Strict type system with explicit mutability:
  - mutable binding/member: `:`
  - immutable binding/member: `::`
- Variable declarations with explicit types.
- Procedure declarations with explicit signatures.
- First-class procedure values via `fn`.
- Callback parameter type marker via `callback`.
- Control flow:
  - `if` / chain / default
  - `while`
  - `for`
- Enums are scoped and strongly typed.
- Artifacts support method members and field mutability:
  - mutable field: `field : T`
  - immutable field: `field :: T`
- Artifact initialization styles:
  - positional: `Artifact { value, value }`
  - named: `Artifact { .field = value }`
- Namespaces/modules can expose constants.

#### Simple Examples

Mutability and declarations:

```simple
counter : i32 = 1
app_name :: string = "Simple"
counter += 1
```

Procedure declaration + first-class `fn` value:

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}

f : fn = i32 (a : i32, b : i32) { return a + b }
answer : i32 = f(20, 22)
```

Callback parameter type:

```simple
run_twice : void (cb : callback) {
  cb()
  cb()
}
```

Control flow chain:

```simple
|> hp <= 0 { state = 0 }
|> hp < 50 { state = 1 }
|> default { state = 2 }
```

Equivalent `if/else` comparison:

```simple
if hp <= 0 {
  state = 0
} else {
  if hp < 50 {
    state = 1
  } else {
    state = 2
  }
}
```

Enum + artifact + method:

```simple
State :: enum { Idle = 0, Moving = 1 }

Player :: artifact {
  hp : i32
  tick : void () { self.hp -= 1 }
}

p : Player = { .hp = 100 }
p.tick()
```

### Import System Overview

- Reserved imports (stdlib/runtime mapped), for example:
  - `System.io`, `System.fs`, `System.dl`, `DL`
- Path/file imports for project code, for example:
  - `"raylib"`
  - `"./raylib.simple"`
  - `"../another/raylib.simple"`
- `extern` declarations define native symbol signatures used by `DL`.

### .simple -> SIR -> Instruction Sequence

Source example:

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}

main : i32 () {
  return add(20, 22)
}
```

Representative SIR shape:

```asm
sigs:
  sig_add = i32(i32, i32)
  sig_main = i32()

func add locals=2 stack=2 sig=sig_add
  enter 2
  load_local 0
  load_local 1
  add_i32
  ret

func main locals=0 stack=2 sig=sig_main
  enter 0
  const_i32 20
  const_i32 22
  call add 2
  ret

entry main
```

Resulting SBC instruction sequence (conceptual):

| Function | Instruction sequence |
|---|---|
| `add` | `enter 2 -> load_local 0 -> load_local 1 -> add_i32 -> ret` |
| `main` | `enter 0 -> const_i32 20 -> const_i32 22 -> call add 2 -> ret` |

The verifier enforces signature and stack correctness for both functions before execution.

## Build Requirements

### Common

- C++17 compiler toolchain
- Git
- Shell environment

### Default Build Mode (recommended)

- CMake 3.16+
- `libffi` development package

### Legacy Build Mode

- Linux/macOS only: `--legacy` on `build_linux` / `build_macos`
- CMake not required in legacy mode

## Install And Build (Authoritative Scripts)

- Linux: `./build_linux`
- macOS: `./build_macos`
- Windows: `./build_windows`

Each script supports source build, package generation, and install flow.

### Linux

```bash
./build_linux --version v0.03.1
```

### macOS

```bash
./build_macos --version v0.03.1
```

### Windows

```bash
./build_windows --version v0.03.1
```

For Windows + vcpkg `libffi`, pass toolchain args via `--cmake-arg` (see `Docs/CLI.md`).

### Install Summary By OS

- Windows:
  - use `./build_windows`
  - CMake + MSVC toolchain
  - `libffi` typically via vcpkg toolchain args
- Linux:
  - use `./build_linux`
  - CMake + C++ toolchain + `libffi-dev`
  - optional `--legacy` mode
- macOS:
  - use `./build_macos`
  - CMake + clang toolchain + Homebrew `libffi`
  - optional `--legacy` mode

## Quick Validation

- Build only: script default behavior
- Build + tests: add `--tests`
- Check PATH visibility after install:

```bash
simple --version
simple -v
```

## Documentation Rules

- Keep one authoritative home per topic.
- If behavior changes in code, update the matching module doc in the same change.
- Keep examples runnable and aligned with current syntax and CLI behavior.
- `Docs/legacy/` is reference-only, not authoritative.
