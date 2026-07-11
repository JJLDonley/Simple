# System Library Plan

`System.*` is the only low-level library root. It is explicit, capability-aware, native/runtime-facing, and close to the Simple VM ABI. It replaces public short imports such as `DL`, `FS`, `Env`, `Path`, `Thread`, and `Channel`.

There are no public compatibility aliases in the target model. Source imports must use `System.X` or `Standard.X`.

## Goals

- Expose host/runtime control without hiding resource, blocking, allocation, GC, or capability behavior.
- Back every API by native metadata shared by interpreter, JIT, future AOT, docs, and LSP.
- Use generational opaque handles for host resources; never expose raw platform handles or VM internals directly.
- Return `Result<T,E>` or `Option<T>` for expected host failures once language support is complete.
- Keep dynamic/native calls metadata-driven. No library-specific shims and no ABI guessing.

## Required modules

| Module | Purpose | Current transitional backing |
|---|---|---|
| `System.IO` | low-level streams | current `IO` / `System.io` native surface |
| `System.FS` | low-level file/dir handles | current `FS`/`File` / `System.fs` |
| `System.Path` | platform path operations | current `Path` / `System.path` |
| `System.Env` | process args/env | current `Env` / `System.env` |
| `System.OS` | platform/process facts | current `OS` / `System.os` |
| `System.Time` | clocks/timers | current `Time` |
| `System.FFI` | dynamic loading/extern FFI | current `DL` / `System.dl` |
| `System.ASM` | C/DynASM/native unit compilation/linking | planned |
| `System.Bytes` | canonical low-level bytes | isolated over the current `System.buffer` runtime backing |
| `System.Json` | isolated low-level JSON handles (`parse`, `stringify`, `free`) | current `System.json` runtime backing |
| `System.Log` | logging sink | current `Log` / `System.log` |
| `System.Random` | low-level RNG | current `Random` / `System.random` |
| `System.Thread` | OS/runtime thread primitives | current `Thread` / `System.thread` |
| `System.Job` | VM jobs/promises | planned on Promise runtime |
| `System.Channel` | low-level channels | current `Channel` / `System.channel` |
| `System.Process` | process spawning/control | planned |
| `System.Net` | sockets/listeners | planned |
| `System.HTTP` | low-level HTTP client/server handles | planned |
| `System.Terminal` | terminal/raw mode/events | planned |
| `System.Capability` | capability inspection/control | capability policy work |
| `System.Runtime` | VM/runtime introspection | planned/debug-gated |
| `System.Debug` | trap/assert/stack trace/breakpoint | planned/debug-gated |

## Metadata contract

Every public `System.*` native function must declare:

- layer: `system`
- module and symbol
- exact Simple signature
- resource inputs/outputs and resource kind
- ownership transfer and cleanup behavior
- blocking behavior
- allocation behavior
- GC/safepoint behavior
- capability tags
- platform availability
- stability status
- doc summary
- JIT loop-safety classification derived from metadata, not guessed

## Migration rule

Short imports are rejected. Public code must use:

```simple
import System.FS
import System.FFI
import System.Time
```

not:

```simple
import FS
import DL
import Time
```

Old imports produce diagnostics with suggested canonical replacements, not aliases. Planned `System.*` modules may be importable before their APIs are implemented; they must not expose duplicate high-level `Standard.*` behavior.

`System.Buffer` is not public: the runtime does not currently provide a native-buffer resource distinct from heap bytes. `System.Bytes` is the sole low-level byte module.
