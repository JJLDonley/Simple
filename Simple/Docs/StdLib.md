# Simple Standard Library Surface (Current)

This document defines reserved imports and their runtime bindings.

This document is authoritative for reserved import names and core module API surface.

## Reserved Import Paths

These paths are compiler-reserved and do not resolve to source files:

- `Math`
- `IO`
- `Time`
- `File`
- `Core.DL`
- `Core.Os`
- `Core.Fs`
- `Core.Log`

## Mapping

| Import Path | Runtime Namespace | Backend |
|---|---|---|
| `Math` | `core.math` | intrinsics |
| `IO` | `core.io` | intrinsics |
| `Time` | `core.time` | intrinsics |
| `File` | `core.fs` | import calls |
| `Core.DL` | `core.dl` | import calls |
| `Core.Os` | `core.os` | import calls |
| `Core.Fs` | `core.fs` | import calls |
| `Core.Log` | `core.log` | import calls |

## Module API Summary

### Math
- `Math.abs(x)`
- `Math.min(a, b)`
- `Math.max(a, b)`
- `Math.PI`

### IO
- `IO.print(x)`
- `IO.println(x)`

### Time
- `Time.mono_ns()`
- `Time.wall_ns()`

### File / Core.Fs
- `open(path: string, flags: i32) -> i32`
- `read(fd: i32, buf: i32[], len: i32) -> i32`
- `write(fd: i32, buf: i32[], len: i32) -> i32`
- `close(fd: i32) -> void`

### Core.Os
- `args_count() -> i32`
- `args_get(index: i32) -> string`
- `env_get(name: string) -> string`
- `cwd_get() -> string`
- `time_mono_ns() -> i64`
- `time_wall_ns() -> i64`
- `sleep_ms(ms: i32) -> void`

### Core.Log
- `log(message: string, level: i32) -> void`

### Core.DL
- `open(path: string) -> i64`
- `sym(handle: i64, name: string) -> i64`
- `close(handle: i64) -> i32`
- `last_error() -> string`

Typed dynamic symbol calls are generated from `extern` signatures through `DL.Open(path, manifest)`.

Dynamic call ABI support (current):

- up to 254 extern parameters per symbol
- scalars: `i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/char/string`
- pointers: `*T` / `*void`
- enums
- artifacts by-value (struct ABI marshalling)

Explicit limitation:

- recursive struct ABI is rejected.

## Import Alias Notes

Compiler alias normalization for extern modules:

- `core_os` -> `core.os`
- `core_fs` -> `core.fs`
- `core_log` -> `core.log`
- `core_dl` -> `core.dl`
