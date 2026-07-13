# System / Standard Library Migration

This document tracks migration from short reserved imports to the no-alias model:

```simple
import System.X
import Standard.X
```

Short public imports are rejected. They are diagnostics only, not compatibility aliases.

## Final import rule

Valid:

```simple
import System.FS
import System.FFI
import Standard.IO
import Standard.FS
```

Invalid in the final model:

```txt
legacy IO import is rejected; use Standard.IO
legacy FS import is rejected; use Standard.FS or System.FS
legacy DL import is rejected; use System.FFI
legacy Time import is rejected; use System.Time or Standard.Time
```

## Current-to-final mapping

| Current import | Final owner | Notes |
|---|---|---|
| `IO` | `Standard.IO` | print/println are high-level |
| `Math` | `Standard.Math` | compiler intrinsics remain internal; add `System.Math` only if low-level native math is needed |
| `Time` | `Standard.Time` / `System.Time` | clocks in System, formatting in Standard |
| `DL` | `System.FFI` | dynamic loading and extern FFI are low-level |
| `OS` | `System.OS` | platform/process facts |
| `File` | `System.FS` | old low-level fd-style API |
| `FS` | `Standard.FS` / `System.FS` | readText-style helpers are Standard; handles are System |
| `Path` | `Standard.Path` / `System.Path` | ergonomic helpers Standard; primitive path ops System |
| `Env` | `System.Env` / `Standard.Env` | raw args/env System; convenience Standard if added |
| `Random` | `Standard.Random` / `System.Random` | raw RNG System; range/bool helpers Standard |
| `Buffer` | `System.Buffer` / `Standard.Buffer` plus `System.Bytes` / `Standard.Bytes` where immutable byte values are distinct | mutable/native/cursor behavior maps to Buffer; immutable byte-value conversion/helpers map to Bytes |
| `Json` | `System.Json` first, `Standard.Json` later | current handle API is System; value API is Standard after variants/object model |
| `Log` | `Standard.Log` / `System.Log` | sink/level System; info/warn/error Standard |
| `Thread` | `System.Thread` | low-level threading |
| `Channel` | `System.Channel` first, `Standard.Channel` later | generic wrapper waits on generics/runtime support |
| `Http` | `System.HTTP` / `Standard.HTTP` | low-level handles System; ergonomic client/server Standard |
| `Socket` | `System.Net` / `Standard.Net` | sockets System; streams/listeners Standard |

## Migration slices

1. Add `docs/System.md`, `docs/Standard.md`, and this inventory.
2. Accept canonical `System.*` and `Standard.*` imports through transitional mapping to current implementations.
3. Update tests, docs, playgrounds, and LSP expectations to canonical imports. Done for in-repo tests/docs.
4. Reject short imports with diagnostics and suggested replacements. Done for reserved imports.
5. Replace transitional mappings with real low-level `System.*` APIs.
6. Implement source-level `Standard.*` wrappers over `System.*`.
7. Switch final APIs to `Result`/`Option`/typed handles and the final Buffer/Bytes split as those language features complete.

## Diagnostics target

Old imports should fail with direct replacements:

```txt
legacy IO      -> use Standard.IO
legacy FS      -> use Standard.FS or System.FS
legacy DL      -> use System.FFI
legacy Buffer  -> use System.Buffer for low-level mutable/native buffers, Standard.Buffer for high-level growable/cursor buffers, or Standard.Bytes/System.Bytes for immutable byte values
legacy Channel -> use System.Channel
```

These are diagnostics, not aliases.

## Duplicate root rule

A planned `Standard.*` module may be reserved before its source-level wrapper exists, but it must not expose raw `System.*` members as a duplicate root. For example, `Standard.Process` must not surface `System.OS.sleepMs`, and `Standard.Console` must not surface `Standard.IO.println`; wrappers land only when their documented Standard API and tests land.
