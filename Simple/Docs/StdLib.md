# Simple Standard Library Layout (v0.1)

This document defines the standardized library layout and naming for Simple. It is a compiler-facing
contract that maps user-facing import paths to core VM namespaces.

## 1) Reserved Import Paths

These import paths are reserved and resolved by the compiler:

- `Math`
- `IO`
- `Time`
- `File`

Using these paths does **not** load files from disk. They map to core VM namespaces and intrinsics.

## 2) Module Mapping

| Import Path | Module Alias | Core Namespace | Backend |
|---|---|---|---|
| `Math` | `Math` | `core.math` | Intrinsics (abs/min/max)
| `IO` | `IO` | `core.io` | Intrinsic print_any (print/println)
| `Time` | `Time` | `core.time` | Intrinsics (mono_ns/wall_ns)
| `File` | `File` | `core.fs` | FFI imports (open/read/write/close)

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

## 4) Core FFI Modules

These FFI modules are resolved via the import table and handled by the VM:

- `core.os` (args/env/cwd/time/sleep)
- `core.fs` (file IO)
- `core.log` (logging)

### Compiler Import Resolver Aliases

To reference core FFI modules from `extern` declarations, the compiler recognizes:

- `core_os` -> `core.os`
- `core_fs` -> `core.fs`
- `core_log` -> `core.log`
