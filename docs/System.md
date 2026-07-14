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
| `System.IO` | low-level streams | native `System.IO` surface |
| `System.FS` | low-level file/dir handles | native `System.FS` surface |
| `System.Path` | platform path operations | native `System.Path` surface |
| `System.Env` | process args/env | native `System.Env` surface |
| `System.OS` | platform/process facts | native `System.OS` surface |
| `System.Time` | clocks/timers | native `System.OS`/time backing |
| `System.FFI` | dynamic loading/extern FFI | runtime-owned library handles and borrowed symbols |
| `System.ASM` | C/DynASM/native unit compilation/linking | planned |
| `System.Buffer` | low-level mutable/native/runtime buffer handles, C/C++-style data handling, FFI/pinning/explicit cleanup semantics | canonical `System.Buffer` runtime backing |
| `System.Bytes` | low-level immutable/owned byte values if distinct from buffers | planned/value-level byte surface |
| `System.Json` | isolated low-level JSON handles (`parse`, `stringify`, `free`) | runtime-owned JSON value handles |
| `System.Log` | logging sink | native `System.Log` surface |
| `System.Random` | low-level RNG | native `System.Random` surface |
| `System.Thread` | OS/runtime thread primitives | native `System.Thread` surface |
| `System.Job` | VM jobs/promises | experimental runtime-owned jobs and synchronized promise registry |
| `System.Channel` | low-level typed channels | runtime-owned channel handles |
| `System.Process` | process spawning/control | experimental runtime-owned process handles |
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

## Resource lifecycle

Current file, FFI library, JSON value, channel, job, and process resources are owned by the executing VM's native resource registry. Packed handles carry slot, generation, and runtime-owner identity. Native dispatch rejects null, foreign-runtime, stale, wrong-kind, and already-closed handles before ordinary input operations. Explicit close/free operations release host state and invalidate the handle; shutdown closes and finalizes any owned resource the program leaves live.

Borrowed text/byte views last only for one native call. Dynamic symbols last only while their owning FFI library remains open. Resource-returning functions declare transfer-to-caller plus VM-shutdown cleanup metadata, borrowed operations declare input metadata, and close/free operations declare transfer-to-callee plus required cleanup.

`System.Job` handles use the same resource lifecycle. Shutdown cancels and wakes pending jobs, joins worker threads, and releases synchronized promise records. The experimental async API is documented in [Jobs and promises](Async.md).

`System.Process` handles own child processes, standard-stream pipes, and output-drain workers. Explicit close or VM shutdown terminates a running child, reaps it, joins readers, and closes host handles. See [Processes](Process.md) for the API and cross-platform contract.

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

Byte/memory naming is intentional:

- `System.Buffer` is public low-level mutable/native/runtime buffer handling: resource-backed handles, C/C++-style binary data operations, explicit ownership/cleanup, pinning/FFI policy, and endian reads/writes.
- `System.Bytes` is reserved for low-level immutable/owned byte values when that value type is distinct from buffers.
- Do not duplicate APIs: mutable/resource/cursor behavior belongs to `System.Buffer`; immutable byte-value behavior belongs to `System.Bytes`.
