# Simple Standard Library Layout (v0.1)

This document defines the standardized library layout and naming for Simple. It is a compiler-facing
contract that maps user-facing import paths to core VM namespaces.

## 1) Reserved Import Paths

These import paths are reserved and resolved by the compiler:

- `Math`
- `IO`
- `Time`
- `File`
- `Core.DL`
- `Core.Os`
- `Core.Fs`
- `Core.Log`

Using these paths does **not** load files from disk. They map to core VM namespaces and intrinsics.

## 2) Module Mapping

| Import Path | Module Alias | Core Namespace | Backend |
|---|---|---|---|
| `Math` | `Math` | `core.math` | Intrinsics (abs/min/max)
| `IO` | `IO` | `core.io` | Intrinsic print_any (print/println)
| `Time` | `Time` | `core.time` | Intrinsics (mono_ns/wall_ns)
| `File` | `File` | `core.fs` | FFI imports (open/read/write/close)
| `Core.DL` | `Core.DL` | `core.dl` | FFI imports (dlopen/dlsym/dlclose)
| `Core.Os` | `Core.Os` | `core.os` | FFI imports (args/env/cwd/time/sleep)
| `Core.Fs` | `Core.Fs` | `core.fs` | FFI imports (open/read/write/close)
| `Core.Log` | `Core.Log` | `core.log` | FFI imports (log)

## 3) Reserved Module APIs

### Math
- `Math.abs(x)` -> intrinsic abs (i32/i64)
- `Math.min(a, b)` -> intrinsic min (i32/i64/f32/f64)
- `Math.max(a, b)` -> intrinsic max (i32/i64/f32/f64)
- `Math.PI` -> `f64` constant

### IO
- `IO.print(x)` -> intrinsic print_any
- `IO.println(x)` -> intrinsic print_any + "\n"

### Time
- `Time.mono_ns()` -> intrinsic mono_ns
- `Time.wall_ns()` -> intrinsic wall_ns

### File
- `File.open(path: string, flags: i32) -> i32`
- `File.read(fd: i32, buf: i32[], len: i32) -> i32`
- `File.write(fd: i32, buf: i32[], len: i32) -> i32`
- `File.close(fd: i32) -> void`

### Core.DL
- `Core.DL.open(path: string) -> i64`
- `Core.DL.sym(handle: i64, name: string) -> i64`
- `Core.DL.close(handle: i64) -> i32`
- `Core.DL.last_error() -> string`
- Typed dynamic symbol calls are emitted from extern signatures used by `DL.Open(path, manifest)`.
- Supported dynamic-call ABI surface:
  - up to 4 ABI parameters (after artifact flattening)
  - 0-2 params: `i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/char/string`
  - 3-4 params: currently `i32`-lane scalars (`i8/i16/i32/u8/u16/u32/bool/char`)
  - return types: `void` and `i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/char/string`
  - artifact parameters are lowered to ordered field scalars

### Core.Os
- `Core.Os.args_count() -> i32`
- `Core.Os.args_get(index: i32) -> string`
- `Core.Os.env_get(name: string) -> string`
- `Core.Os.cwd_get() -> string`
- `Core.Os.time_mono_ns() -> i64`
- `Core.Os.time_wall_ns() -> i64`
- `Core.Os.sleep_ms(ms: i32) -> void`

### Core.Fs
- `Core.Fs.open(path: string, flags: i32) -> i32`
- `Core.Fs.read(fd: i32, buf: i32[], len: i32) -> i32`
- `Core.Fs.write(fd: i32, buf: i32[], len: i32) -> i32`
- `Core.Fs.close(fd: i32) -> void`

### Core.Log
- `Core.Log.log(message: string, level: i32) -> void`

## 4) Core FFI Modules

These FFI modules are resolved via the import table and handled by the VM:

- `core.os` (args/env/cwd/time/sleep)
- `core.fs` (file IO)
- `core.log` (logging)
- `core.dl` (dlopen/dlsym/dlclose + last_error)

### core.dl
- `core.dl.open(path: string) -> i64`
- `core.dl.sym(handle: i64, name: string) -> i64`
- `core.dl.close(handle: i64) -> i32`
- `core.dl.last_error() -> string`
- `core.dl.call_i32(ptr: i64, a: i32, b: i32) -> i32`
- `core.dl.call_i64(ptr: i64, a: i64, b: i64) -> i64`
- `core.dl.call_f32(ptr: i64, a: f32, b: f32) -> f32`
- `core.dl.call_f64(ptr: i64, a: f64, b: f64) -> f64`
- `core.dl.call_str0(ptr: i64) -> string`

### Compiler Import Resolver Aliases

To reference core FFI modules from `extern` declarations, the compiler recognizes:

- `core_os` -> `core.os`
- `core_fs` -> `core.fs`
- `core_log` -> `core.log`
- `core_dl` -> `core.dl`
