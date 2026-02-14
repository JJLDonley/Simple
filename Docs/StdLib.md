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
- `List`

Preferred modern aliases:
- `System.math`
- `System.io`
- `System.time`
- `System.fs`
- `System.dl`
- `System.os`
- `System.log`
- `System.list`

Reserved import keywords are case-insensitive.

## Import Mapping

| Import | Runtime Namespace |
|---|---|
| `Math` / `System.math` | `core.math` |
| `IO` / `System.io` | `core.io` |
| `Time` / `System.time` | `core.time` |
| `File` / `FS` / `System.fs` | `core.fs` |
| `DL` / `System.dl` | `core.dl` |
| `OS` / `System.os` | `core.os` |
| `Log` / `System.log` | `core.log` |
| `List` / `System.list` | `core.list` |

## Core Module API Tables

### Math
| Member | Signature |
|---|---|
| `abs` | `abs(x)` |
| `min` | `min(a, b)` |
| `max` | `max(a, b)` |
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

### DL
| Member | Signature |
|---|---|
| `open` | `(path : string) -> i64` |
| `sym` | `(handle : i64, name : string) -> i64` |
| `close` | `(handle : i64) -> i32` |
| `last_error` | `() -> string` |
| `supported` | `bool` constant |

### List
Lists grow automatically (like `std::vector`) on `push`/`insert`. `List.new<T>` currently requires an integer literal capacity.

| Member | Signature |
|---|---|
| `new<T>` | `(capacity : i32) -> T[]` |
| `len<T>` | `(list : T[]) -> i32` |
| `push<T>` | `(list : T[], value : T) -> void` |
| `pop<T>` | `(list : T[]) -> T` |
| `insert<T>` | `(list : T[], index : i32, value : T) -> void` |
| `remove<T>` | `(list : T[], index : i32) -> T` |
| `clear<T>` | `(list : T[]) -> void` |

## Extern Interop
`extern` signatures define ABI contracts for dynamic symbol calls.
- signatures are strict
- no implicit ABI coercion
- artifacts by value supported (non-recursive)

See `Docs/VM.md` for runtime ABI details.
