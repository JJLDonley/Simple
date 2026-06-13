# Simple Standard Library (API)

This document defines reserved imports and core runtime module APIs.

## Supported
- Reserved import keywords mapped to runtime namespaces.
- Modern `System.*` aliases and legacy short names.
- Core module APIs listed in this document.
- `extern` declarations as ABI contracts for `DL` dynamic calls.

## Not Supported
- Modules or members not listed here.
- Implicit ABI coercion for `extern` calls.
- Recursive artifact structs in `extern` ABI (rejected by the runtime).

## Planned
- Expanded standard library surface beyond the current core modules.
- Formalized ABI compatibility guidance for `extern` contracts.

## Reserved Import Keywords
Reserved (compiler-mapped):
- `Math`
- `IO`
- `Time`
- `File`
- `DL`
- `OS`
- `FS`
- `Log`
- `Buffer`
- `Json`
- `Channel`

Preferred modern aliases:
- `System.math`
- `System.io`
- `System.time`
- `System.fs`
- `System.dl`
- `System.os`
- `System.log`
- `System.buffer`
- `System.json`
- `System.channel`

## Import Mapping

| Import | Runtime Namespace |
|---|---|
| `Math` / `System.math` | `System.math` |
| `IO` / `System.io` | `System.io` |
| `Time` / `System.time` | `System.time` |
| `File` / `FS` / `System.fs` | `System.fs` |
| `DL` / `System.dl` | `System.dl` |
| `OS` / `System.os` | `System.os` |
| `Log` / `System.log` | `System.log` |
| `Buffer` / `System.buffer` | `System.buffer` |
| `Json` / `System.json` | `System.json` |
| `Channel` / `System.channel` | `System.channel` |

## Core Module API Tables

### Math
| Member | Signature |
|---|---|
| `abs` | `abs(x)` |
| `min` | `min(a, b)` |
| `max` | `max(a, b)` |
| `sqrt` | `sqrt(x)` |
| `PI` | constant |

### IO
| Member | Signature |
|---|---|
| `print` | `print(x)` or `print("fmt {}", args...)` |
| `println` | `println(x)` or `println("fmt {}", args...)` |
| `buffer_new` | `(length : i32) -> i32[]` |
| `buffer_len` | `(buffer : i32[]) -> i32` |
| `buffer_fill` | `(buffer : i32[], value : i32, count : i32) -> i32` |
| `buffer_copy` | `(dst : i32[], src : i32[], count : i32) -> i32` |

### Time
| Member | Signature |
|---|---|
| `mono_ns` | `() -> i64` |
| `wall_ns` | `() -> i64` |

### Json
| Member | Signature |
|---|---|
| `parse` | `(text : string) -> i64` (0 on invalid JSON) |
| `stringify` | `(handle : i64) -> string` |
| `free` | `(handle : i64) -> bool` |

### Buffer
| Member | Signature |
|---|---|
| `new` | `(length : i32) -> i32[]` |
| `len` | `(buffer : i32[]) -> i32` |
| `readU16LE` | `(buffer : i32[], offset : i32) -> i32` |
| `readU32LE` | `(buffer : i32[], offset : i32) -> i32` |
| `writeU16LE` | `(buffer : i32[], offset : i32, value : i32) -> bool` |
| `writeU32LE` | `(buffer : i32[], offset : i32, value : i32) -> bool` |
| `slice` | `(buffer : i32[], offset : i32, count : i32) -> i32[]` |
| `copy` | `(dst : i32[], dstOffset : i32, src : i32[], srcOffset : i32, count : i32) -> i32` |

### Channel
| Member | Signature |
|---|---|
| `newI32`/`newI64`/`newF32`/`newF64`/`newBool`/`newString`/`newBytes` | `() -> i64` |
| `send*`/`trySend*` | `(handle : i64, value : T) -> bool` |
| `recv*`/`tryRecv*` | `(handle : i64) -> T` |
| `pending*` | `(handle : i64) -> i32` queued values for non-blocking polling |
| `close` | `(handle : i64) -> void` |

### Fs
| Member | Signature |
|---|---|
| `open` | `(path : string, flags : i32) -> i32` |
| `read` | `(fd : i32, buf : i32[], len : i32) -> i32` |
| `write` | `(fd : i32, buf : i32[], len : i32) -> i32` |
| `close` | `(fd : i32) -> void` |

### Os
| Member | Signature |
|---|---|
| `args_count` | `() -> i32` |
| `args_get` | `(index : i32) -> string` |
| `env_get` | `(name : string) -> string` |
| `cwd_get` | `() -> string` |
| `time_mono_ns` | `() -> i64` |
| `time_wall_ns` | `() -> i64` |
| `sleep_ms` | `(ms : i32) -> void` |
| `is_linux` | `bool` constant |
| `is_macos` | `bool` constant |
| `is_windows` | `bool` constant |
| `has_dl` | `bool` constant |

### Log
| Member | Signature |
|---|---|
| `log` | `(message : string, level : i32) -> void` |
| `info` | `(message : string) -> void` |
| `warn` | `(message : string) -> void` |
| `error` | `(message : string) -> void` |
| `setLevel` | `(level : i32) -> void` |
| `setFile` | `(path : string) -> bool` (empty path restores stdout/stderr) |

### DL
| Member | Signature |
|---|---|
| `open` | `(path : string) -> i64` |
| `sym` | `(handle : i64, name : string) -> i64` |
| `close` | `(handle : i64) -> i32` |
| `last_error` | `() -> string` |
| `supported` | `bool` constant |

## Extern Interop
`extern` signatures define ABI contracts for dynamic symbol calls.
- signatures are strict
- no implicit ABI coercion
- artifacts by value supported (non-recursive)

See `Docs/VM.md` for runtime ABI details.
