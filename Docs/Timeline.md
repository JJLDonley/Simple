# Simple Timeline

This is the canonical project roadmap. Work is organized as `v0.X.Y` release slices so each patch release lands one coherent, testable milestone.

Current baseline:

- `v0.5.0` released.
- Full suite is green: `1809/1809`.
- JIT suite is green: `61/61`.
- CLI runs use JIT by default when LLVM ORC support is available; `-int` / `--interpreter` forces interpreter execution.
- Public imports are canonical only: `System.X` and `Standard.X`.
- Library catalog is enum-backed with centralized signatures/metadata for compiler, LSP, native validation, and docs.

## Locked architecture decisions

- Simple is identity-first, strictly typed, and has no implicit coercion.
- Source compiles to SIR, then portable SBC bytecode, then runs on the Simple VM.
- The interpreter is the semantic baseline.
- LLVM ORC JIT may only accelerate validated SBC and must fallback/trap safely when unsupported.
- SBC remains the portable artifact. JIT output is platform-local machine code, not a distribution format.
- Native calls are metadata-driven. Interpreter, JIT, future AOT, docs, LSP, and reserved signatures consume the same catalog/native metadata.
- Host resources are generational opaque handles. Raw platform handles and VM heap internals are not exposed directly to Simple code.
- `System.*` is explicit, low-level, capability-aware, and native/runtime-facing.
- `Standard.*` is ergonomic and builds on `System.*`.
- No public short aliases. Legacy names exist only in explicit diagnostics/migration quarantine.

---

# Release roadmap

## `v0.5.0` — Canonical library architecture and JIT baseline ✅

Status: shipped.

Delivered:

- Canonical `System.*` / `Standard.*` import model.
- No public aliases such as `IO`, `FS`, `DL`, `Time`, `Buffer`, `Channel`.
- Enum-backed library catalog boundary in `Library/include/library_catalog.h`.
- Centralized library signatures and availability/backing metadata.
- Native registry validation against catalog metadata where signatures exist.
- Legacy name helpers quarantined in `Library/src/library_legacy.cpp`.
- CLI JIT default with interpreter override.
- JIT suite green.
- Full suite green.
- Docs and website aligned to v0.5 public model.

---

## `v0.5.1` — Native resource foundation

Goal: make all host/runtime resources safe, inspectable, and consistently owned before expanding native libraries.

Deliverables:

- [ ] Harden native resource registry.
- [ ] Standardize handle layout: kind, index, generation, closed state.
- [ ] Add wrong-kind/stale/closed handle diagnostics.
- [ ] Add close/finalize/shutdown sweep behavior tests.
- [ ] Add resource metadata to catalog/native specs:
  - [ ] resource inputs
  - [ ] resource outputs
  - [ ] ownership transfer
  - [ ] cleanup/finalizer behavior
  - [ ] blocking/allocation/GC-safepoint flags
- [ ] Make native shutdown cleanup deterministic and test-covered.
- [ ] Add docs for native resource lifecycle.
- [ ] Keep full suite and JIT suite green.

Exit criteria:

- Every native resource used by current libraries has a kind and validation path.
- Resource tests cover normal close, double close, stale handle, wrong kind, and VM shutdown sweep.

---

## `v0.5.2` — Promise / Job foundation

Goal: introduce the minimal async runtime needed by process, network, HTTP, terminal, and future async language syntax.

Deliverables:

- [ ] Implement `System.Job` minimal runtime surface.
- [ ] Implement `Standard.Promise` minimal surface.
- [ ] Promise states:
  - [ ] pending
  - [ ] completed
  - [ ] failed
  - [ ] cancelled
- [ ] Promise operations:
  - [ ] `run`
  - [ ] `await`
  - [ ] `poll`
  - [ ] `cancel`
  - [ ] `isDone`
- [ ] Blocking `await` and non-blocking `poll` behavior.
- [ ] Promise result/error storage.
- [ ] Promise cleanup and cancellation tests.
- [ ] GC/root safety restrictions for async boundaries.
- [ ] No language-level `async fn` syntax required yet unless it falls out naturally.
- [ ] Docs and website examples marked implemented/WIP accurately.

Exit criteria:

- Simple code can create, await, poll, and cancel Promise-backed jobs through library APIs.
- Promise/job behavior is deterministic and test-covered.

---

## `v0.5.3` — Process library

Goal: usable process execution from Simple.

Deliverables:

- [ ] Implement `System.Process` core resource API:
  - [ ] `spawn`
  - [ ] `wait`
  - [ ] `kill`
  - [ ] `stdin`
  - [ ] `stdout`
  - [ ] `stderr`
- [ ] Implement `Standard.Process` ergonomic API:
  - [ ] `run`
  - [ ] `runText`
  - [ ] async process wrapper using Promise if available
- [ ] Process resource cleanup on close/VM shutdown.
- [ ] Cross-platform behavior notes and platform-specific tests where feasible.
- [ ] Capability metadata for process spawn/kill/stdio.
- [ ] Docs, LSP signatures, and examples.

Exit criteria:

- Simple can run a child process, collect exit status/stdout/stderr, and clean up handles.

---

## `v0.5.4` — Network TCP foundation

Goal: usable local TCP networking from Simple.

Deliverables:

- [ ] Implement `System.Net` TCP resource API:
  - [ ] `tcpConnect`
  - [ ] `tcpListen`
  - [ ] `accept`
  - [ ] `send`
  - [ ] `recv`
  - [ ] `close`
- [ ] Socket/listener resource kinds and cleanup.
- [ ] Blocking behavior metadata.
- [ ] Loopback client/server tests.
- [ ] Error/timeout behavior documented.
- [ ] Decide whether `Standard.Net` lands in this slice or remains WIP until later.

Exit criteria:

- A Simple test can start a loopback listener, connect, exchange bytes/text, and close cleanly.

---

## `v0.5.5` — HTTP client foundation

Goal: first useful HTTP API from Simple.

Deliverables:

- [ ] Implement minimal `System.HTTP` backing or helper runtime for client requests.
- [ ] Implement `Standard.HTTP` client API:
  - [ ] `get`
  - [ ] `post`
  - [ ] status
  - [ ] headers
  - [ ] body text/bytes
- [ ] Promise-backed async HTTP variants if `v0.5.2` is ready.
- [ ] Local test server fixtures.
- [ ] Capability metadata for network client behavior.
- [ ] Docs and examples clearly mark HTTP server as later/WIP unless implemented.

Exit criteria:

- Simple can perform local HTTP GET/POST tests and inspect status/body.

---

## `v0.5.6` — Terminal foundation

Goal: usable terminal control and event polling.

Deliverables:

- [ ] Implement `System.Terminal` core API:
  - [ ] `open`
  - [ ] `close`
  - [ ] `enterRaw`
  - [ ] `exitRaw`
  - [ ] `enterAltScreen`
  - [ ] `exitAltScreen`
  - [ ] `size`
  - [ ] `write`
  - [ ] `writeAt`
  - [ ] `flush`
  - [ ] `pollEvent`
  - [ ] `readEvent`
- [ ] Implement minimal `Standard.Terminal` wrappers.
- [ ] Restore terminal mode on normal exit and supported trap/error paths.
- [ ] Platform support matrix.
- [ ] Tests for non-interactive-safe pieces.
- [ ] Manual playground/demo for interactive terminal behavior.

Exit criteria:

- Simple can open terminal control, write, query size where supported, and restore state safely.

---

## `v0.5.7` — Threads, channels, and concurrency polish

Goal: make concurrency primitives coherent together.

Deliverables:

- [ ] Stabilize `System.Thread`:
  - [ ] `yield`
  - [ ] `sleepMs`
  - [ ] `hardwareConcurrency`
  - [ ] spawn/join/detach only if closure/rooting is safe
- [ ] Stabilize `System.Channel` typed families:
  - [ ] `I32`
  - [ ] `I64`
  - [ ] `F32`
  - [ ] `F64`
  - [ ] `Bool`
  - [ ] `String`
  - [ ] `Bytes`
- [ ] Define close/wakeup/cancel semantics.
- [ ] Add Promise/thread/channel interaction tests.
- [ ] Add GC/root-safety stress tests around blocking waits.
- [ ] Decide whether `Standard.Channel` generic wrapper lands or remains WIP.

Exit criteria:

- Threads/channels/promises can be used together in tested non-racy patterns.

---

## `v0.5.8` — Core System/Standard completion pass

Goal: finish the non-async core libraries so scripts feel complete.

System domains:

- [ ] `System.IO`
- [ ] `System.FS`
- [ ] `System.Path`
- [ ] `System.Env`
- [ ] `System.OS`
- [ ] `System.Time`
- [ ] `System.FFI`
- [ ] `System.Buffer`
- [ ] `System.Bytes`
- [ ] `System.Json`
- [ ] `System.Log`
- [ ] `System.Random`

Standard domains:

- [ ] `Standard.IO`
- [ ] `Standard.FS`
- [ ] `Standard.Path`
- [ ] `Standard.Buffer`
- [ ] `Standard.Bytes`
- [ ] `Standard.Text`
- [ ] `Standard.Json`
- [ ] `Standard.Math`
- [ ] `Standard.Random`
- [ ] `Standard.Time`
- [ ] `Standard.Log`

Deliverables:

- [ ] All implemented members have centralized signatures.
- [ ] All implemented native-backed members validate against native metadata.
- [ ] All unavailable members produce clear diagnostics.
- [ ] Every module has direct import and `using` tests where applicable.
- [ ] Docs and website module tables match catalog status.

Exit criteria:

- The core library surface is coherent, documented, and fully green.

---

## `v0.5.9` — Generated docs, LSP coverage, and release hardening

Goal: prepare `v0.6.0` by removing drift between code, docs, LSP, and release packages.

Deliverables:

- [ ] Generate library reference from catalog metadata.
- [ ] Add/refresh docs for every implemented member:
  - [ ] signature
  - [ ] summary
  - [ ] availability
  - [ ] backing
  - [ ] resource behavior
  - [ ] blocking/allocation behavior
- [ ] LSP coverage for all green domains:
  - [ ] completion
  - [ ] hover
  - [ ] signature help
  - [ ] semantic tokens
  - [ ] document links
- [ ] Website examples all compile or are explicitly marked WIP.
- [ ] Cross-platform release workflow green:
  - [ ] Linux
  - [ ] macOS
  - [ ] Windows
- [ ] Full suite green.
- [ ] JIT suite green.
- [ ] Install verification green.

Exit criteria:

- Release branch can be cut to `v0.6.0` without additional cleanup.

---

## `v0.6.0` — Native async library stable

Goal: ship the first stable native/async library foundation.

`v0.6.0` can be cut when:

- [ ] native resource foundation is stable;
- [ ] Promise/Job foundation is usable;
- [ ] process library is usable;
- [ ] TCP networking is usable;
- [ ] HTTP client is usable;
- [ ] terminal foundation is usable;
- [ ] threads/channels/concurrency behavior is coherent;
- [ ] core `System.*` / `Standard.*` libraries are complete enough for practical scripts;
- [ ] unavailable domains are clearly WIP, not accidental APIs;
- [ ] docs and website match implementation;
- [ ] full suite is green;
- [ ] JIT suite is green;
- [ ] release workflow is green on Linux/macOS/Windows.

---

# WIP / deferred after `v0.6.0`

These are important but should not block the `v0.6.0` native async library target unless explicitly pulled forward:

- Full HTTP server framework.
- HTTPS/TLS production-grade support.
- UDP and advanced socket options.
- Language-level `async fn` syntax and `await` expression sugar.
- AOT/native packaging.
- Major GC replacement or MMTk/Boehm migration.
- High-performance async IO reactor.
- Advanced scheduler/work stealing.
- Full terminal UI framework.
- `System.ASM` / native-code-generation pipeline.

---

# v0.6 audit reference

Detailed domain audit: [`LibraryDomainAudit_v0.6.md`](LibraryDomainAudit_v0.6.md).

That audit should be updated as each `v0.5.x` slice lands.
