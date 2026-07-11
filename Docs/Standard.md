# Standard Library Plan

`Standard.*` is the only high-level library root. It is ergonomic, safe-by-default, and built on top of `System.*`. It replaces public short imports such as `IO`, `FS`, `Path`, `Time`, `Random`, `Json`, and `Log`.

There are no public compatibility aliases in the target model. Source imports must use `System.X` or `Standard.X`.

## Goals

- Provide user-facing convenience APIs without bypassing `System.*` resource ownership, capability checks, or ABI rules.
- Prefer source-level Simple modules where possible.
- Use `Result<T,E>` and `Option<T>` for expected failures/absence once the language surface is complete.
- Document which `System.*` APIs each Standard API wraps.
- Keep unsafe/host-control behavior in `System.*`; Standard composes and owns ergonomics.

## Required modules

| Module | Purpose | Wraps |
|---|---|---|
| `Standard.IO` | print/println/readLine | `System.IO` |
| `Standard.Console` | user-friendly console colors/clear/readline | `System.IO`, `System.Terminal` |
| `Standard.FS` | readText/writeText/readBytes/writeBytes/list/walk | `System.FS`, `System.Path`, `System.Bytes` |
| `Standard.Path` | ergonomic path helpers | `System.Path` |
| `Standard.Bytes` | byte creation/conversion/hex/base64 | `System.Bytes` |
| `Standard.Text` | string/text helpers | runtime string helpers |
| `Standard.Json` | high-level JSON value API | `System.Json` |
| `Standard.Math` | math helpers/intrinsics | compiler intrinsics / `System.Math` |
| `Standard.Random` | range/bool/bytes helpers | `System.Random`, `System.Bytes` |
| `Standard.Time` | now/mono/sleep/format/durations | `System.Time` |
| `Standard.Log` | debug/info/warn/error | `System.Log` |
| `Standard.Process` | run/runText/async process helpers | `System.Process`, `System.IO`, `System.Bytes` |
| `Standard.Net` | high-level TCP streams/listeners | `System.Net` |
| `Standard.HTTP` | HTTP client/server helpers | `System.HTTP`, `System.Net`, `System.Bytes`, `System.Json` |
| `Standard.HTTPS` | secure HTTP helpers | `System.HTTP`, `System.FS`, `System.Net` |
| `Standard.Terminal` | high-level terminal sessions/raw/alt helpers | `System.Terminal` |
| `Standard.Promise` | Promise helpers | `System.Job` |
| `Standard.Channel` | generic channel wrappers | `System.Channel` |
| `Standard.Collections` | List/Map/Set/Queue/Stack | source-level generics |
| `Standard.Result` | Result helpers | language `Result<T,E>` |
| `Standard.Option` | Option helpers | language `Option<T>` |

## API documentation contract

Every `Standard.*` API must document:

- canonical signature
- which `System.*` functions it wraps
- allocation behavior
- blocking behavior
- returned `Result`/`Option` shape
- cleanup behavior
- examples
- tests

## Migration rule

Final public code must use:

```simple
import Standard.IO
import Standard.FS
import Standard.Path
```

not:

```simple
import Standard.IO
import Standard.FS
import Standard.Path
```

Old imports must eventually produce diagnostics with canonical replacements, not aliases.
