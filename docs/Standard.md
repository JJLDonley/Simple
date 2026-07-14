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
| `Standard.FS` | readText/writeText/readBytes/writeBytes/list/walk | `System.FS`, `System.Path`, `System.Buffer`, `System.Bytes` |
| `Standard.Path` | ergonomic path helpers | `System.Path` |
| `Standard.Buffer` | ergonomic mutable/growable/cursor buffer builder, reader, and writer utilities | `System.Buffer`, `System.Bytes` |
| `Standard.Bytes` | immutable byte-value creation/conversion/hex/base64 helpers | `System.Bytes`, `System.Buffer` when conversion needs runtime buffers |
| `Standard.Text` | string/text helpers | runtime string helpers |
| `Standard.Json` | reserved but intentionally unavailable until a real high-level JSON value/wrapper API exists | `System.Json` |
| `Standard.Math` | math helpers/intrinsics | compiler intrinsics |
| `Standard.Random` | range/bool/bytes helpers | `System.Random`, `System.Bytes`, `System.Buffer` |
| `Standard.Time` | now/mono/sleep/format/durations | `System.Time` |
| `Standard.Log` | debug/info/warn/error | `System.Log` |
| `Standard.Process` | run/runText/async process helpers | `System.Process`, `System.IO`, `System.Buffer`, `System.Bytes` |
| `Standard.Net` | high-level TCP streams/listeners | `System.Net` |
| `Standard.HTTP` | HTTP client/server helpers | `System.HTTP`, `System.Net`, `System.Buffer`, `System.Bytes`, `System.Json` |
| `Standard.HTTPS` | secure HTTP helpers | `System.HTTP`, `System.FS`, `System.Net` |
| `Standard.Terminal` | high-level terminal sessions/raw/alt helpers | `System.Terminal` |
| `Standard.Promise` | experimental run/await/poll/cancel/state helpers | `System.Job` |
| `Standard.Channel` | generic channel wrappers | `System.Channel` |
| `Standard.Collections` | List/Map/Set/Queue/Stack | source-level generics |
| `Standard.Result` | Result helpers | language `Result<T,E>` |
| `Standard.Option` | Option helpers | language `Option<T>` |

The current `Standard.Promise` surface wraps runtime-owned `System.Job` handles. It is scalar-only, requires explicit `close`, and does not execute Simple closures on workers. See [Jobs and promises](Async.md).

`Standard.Process` provides synchronous exit-status, text, and byte capture plus Promise-backed asynchronous execution. Async process jobs carry only copied host arguments, exit status, errors, cancellation state, and Promise identity across the worker boundary. See [Processes](Process.md).

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
import IO
import FS
import Path
```

Old imports produce diagnostics with canonical replacements, not aliases. Planned `Standard.*` modules may be importable before their wrappers are implemented; they must not expose unrelated `System.*` members as compatibility aliases.
