# Simple Timeline to v1.0

This is the canonical project roadmap and release contract. Work is organized as `v0.X.Y` release slices so each patch release lands one coherent, testable milestone.

The roadmap is intentionally foundation-first:

- `v0.5` established the canonical import/library/JIT baseline.
- `v0.6` makes the native runtime and async-capable libraries useful.
- `v0.7` completes the core language surface on top of that runtime.
- `v0.8` hardens the VM, GC, JIT, AOT, verifier, and performance story.
- `v0.9` finishes project tooling, packages, distribution, docs, and ecosystem usability.
- `v1.0` freezes the stability contract.

Current baseline:

- `v0.5.0` released.
- Full suite is green: `1809/1809`.
- JIT suite is green: `61/61`.
- CLI runs use JIT by default when LLVM ORC support is available; `-int` / `--interpreter` forces interpreter execution.
- Public imports are canonical only: `System.X` and `Standard.X`.
- Library catalog is enum-backed with centralized signatures/metadata for compiler, LSP, native validation, and docs.

---

## Locked architecture decisions

These decisions are treated as compatibility anchors unless explicitly revised by a major roadmap update.

- Simple is identity-first, strictly typed, and has no implicit coercion.
- Source compiles to SIR, then portable SBC bytecode, then runs on the Simple VM.
- The interpreter is the semantic baseline.
- LLVM ORC JIT may only accelerate validated SBC and must fallback/trap safely when unsupported.
- SBC remains the portable artifact. JIT output is platform-local machine code, not a distribution format.
- Releases use one SemVer tag with explicit `int` and `llvm` artifact flavors; flavor names are not
  separate versions. Linux and macOS publish both tested flavors, while Windows publishes `int`
  until LLVM packaging is stable.
- Native calls are metadata-driven. Interpreter, JIT, future AOT, docs, LSP, and reserved signatures consume the same catalog/native metadata.
- Host resources are generational opaque handles. Raw platform handles and VM heap internals are not exposed directly to Simple code.
- `System.*` is explicit, low-level, capability-aware, and native/runtime-facing.
- `Standard.*` is ergonomic and builds on `System.*`.
- No public short aliases. Legacy names exist only in explicit diagnostics/migration quarantine.
- A release slice is complete only when implementation, tests, docs, LSP/catalog metadata, install verification, and release notes are aligned.

---

# Release acceptance rules

Every non-doc release slice should satisfy:

- [ ] Build succeeds locally.
- [ ] Every published `int`/`llvm` release flavor builds and runs the full suite.
- [ ] `git diff --check` passes.
- [ ] Full test suite is green.
- [ ] JIT section is green where applicable.
- [ ] New APIs have catalog signatures and native metadata.
- [ ] New APIs have LSP completion/hover/signature coverage where applicable.
- [ ] New APIs have docs and at least one example.
- [ ] Unsupported platforms return clear diagnostics or are marked unavailable.
- [ ] The active install is verified when cutting an installable release.
- [ ] Website docs are updated for user-facing changes.

Docs-only compiler commits may use `[skip ci]`. Runtime/compiler/library changes should not use `[skip ci]` unless explicitly justified.

---

# `v0.5` — Canonical baseline and cleanup gate

Before starting `v0.5.1`, complete the immediate cleanup gate in [`CodebaseCleanup_v0.5.md`](CodebaseCleanup_v0.5.md). The cleanup gate prevents test drift, stale diagnostics, catalog/string duplication, and native registry bloat before the native resource foundation work begins.

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

# `v0.6` — Native libraries, async runtime, and VM stability

Theme:

> Make Simple useful for real host interaction: files, processes, networking, HTTP, terminal control, promises, threads, channels, and safe resource cleanup.

`v0.6` is runtime/library-first. Language-level sugar such as `async fn` may be prototyped only if it does not destabilize the native runtime foundation.

## `v0.5.1` — Native resource foundation

Goal: make all host/runtime resources safe, inspectable, and consistently owned before expanding native libraries.

Deliverables:

- [ ] Harden native resource registry.
- [ ] Standardize handle layout:
  - [ ] resource kind
  - [ ] index
  - [ ] generation
  - [ ] closed state
  - [ ] owner VM/runtime identity if needed
- [ ] Add wrong-kind, stale, closed, null/invalid handle diagnostics.
- [ ] Add close/finalize/shutdown sweep behavior tests.
- [ ] Add resource metadata to catalog/native specs:
  - [ ] resource inputs
  - [ ] resource outputs
  - [ ] ownership transfer
  - [ ] cleanup/finalizer behavior
  - [ ] blocking behavior
  - [ ] allocation behavior
  - [ ] GC safepoint behavior
  - [ ] platform availability
  - [ ] capability requirements
- [ ] Make native shutdown cleanup deterministic and test-covered.
- [ ] Define native resource lifetime rules for strings, arrays, bytes, and handles crossing native calls.
- [ ] Document native resource lifecycle.

Exit criteria:

- Every native resource used by current libraries has a kind and validation path.
- Resource tests cover normal close, double close, stale handle, wrong kind, and VM shutdown sweep.

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
  - [ ] `isFailed`
  - [ ] `isCancelled`
- [ ] Blocking `await` and non-blocking `poll` behavior.
- [ ] Promise result/error storage.
- [ ] Promise cancellation and cleanup tests.
- [ ] Define what values may cross async boundaries in v0.6.
- [ ] Add GC/root safety restrictions for async boundaries.
- [ ] Add deadlock and shutdown behavior tests.
- [ ] No language-level `async fn` syntax required yet unless it falls out naturally.

Exit criteria:

- Simple code can create, await, poll, and cancel Promise-backed jobs through library APIs.
- Promise/job behavior is deterministic and test-covered.

## `v0.5.3` — Process library

Goal: usable process execution from Simple.

Deliverables:

- [ ] Implement `System.Process` core resource API:
  - [ ] `spawn`
  - [ ] `wait`
  - [ ] `kill`
  - [ ] `exitCode`
  - [ ] `stdin`
  - [ ] `stdout`
  - [ ] `stderr`
  - [ ] `close`
- [ ] Implement `Standard.Process` ergonomic API:
  - [ ] `run`
  - [ ] `runText`
  - [ ] `runBytes`
  - [ ] async process wrapper using Promise if available
- [ ] Process resource cleanup on close/VM shutdown.
- [ ] Cross-platform behavior notes and platform-specific tests where feasible.
- [ ] Capability metadata for process spawn/kill/stdio.
- [ ] Diagnostics for unsupported pipe/stream modes.

Exit criteria:

- Simple can run a child process, collect exit status/stdout/stderr, and clean up handles.

## `v0.5.4` — Network TCP foundation

Goal: usable local TCP networking from Simple.

Deliverables:

- [ ] Implement `System.Net` TCP resource API:
  - [ ] `tcpConnect`
  - [ ] `tcpListen`
  - [ ] `accept`
  - [ ] `send`
  - [ ] `recv`
  - [ ] `shutdown`
  - [ ] `close`
- [ ] Socket/listener resource kinds and cleanup.
- [ ] Blocking behavior metadata.
- [ ] Timeout behavior or explicit no-timeout policy.
- [ ] Loopback client/server tests.
- [ ] Error behavior documented.
- [ ] Decide whether `Standard.Net` lands in this slice or remains WIP until later.

Exit criteria:

- A Simple test can start a loopback listener, connect, exchange bytes/text, and close cleanly.

## `v0.5.5` — HTTP client foundation

Goal: first useful HTTP API from Simple.

Deliverables:

- [ ] Implement minimal `System.HTTP` backing or helper runtime for client requests.
- [ ] Implement `Standard.HTTP` client API:
  - [ ] `get`
  - [ ] `post`
  - [ ] status
  - [ ] headers
  - [ ] body text
  - [ ] body bytes
- [ ] Promise-backed async HTTP variants if `v0.5.2` is ready.
- [ ] Local test server fixtures.
- [ ] Capability metadata for network client behavior.
- [ ] Docs and examples clearly mark HTTP server as later/WIP unless implemented.

Exit criteria:

- Simple can perform local HTTP GET/POST tests and inspect status/body.

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

## `v0.6.0` — Native async library stable

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

# `v0.7` — Language completion

Theme:

> Make the language feel complete, expressive, and stable on top of the native async runtime.

`v0.7` is language-first. Runtime work is allowed only when required to support language semantics.

## `v0.7.1` — Monomorphic generics completion

Goal: finish concrete generic specialization as a reliable language feature.

Deliverables:

- [ ] Complete generic declaration metadata in TAST/SIR/SBC.
- [ ] Complete concrete instantiation request collection from:
  - [ ] call sites
  - [ ] type annotations
  - [ ] literals
  - [ ] fields
  - [ ] globals
  - [ ] imports
  - [ ] native signatures
- [ ] Insert specialization consistently after TAST validation and before lowering.
- [ ] Reject recursive value containment without indirection.
- [ ] Allow recursion through pointer/ref/handle where safe.
- [ ] Stabilize deterministic generic symbol mangling.
- [ ] Add diagnostics for missing/incompatible type arguments.
- [ ] Add tests for generic functions, data, artifacts, methods, arrays/lists, handles, channels, options, results, promises.

Exit criteria:

- Generic code emits concrete specialized declarations and has stable diagnostics.

## `v0.7.2` — `Result<T,E>` and error propagation

Goal: make expected failure explicit and ergonomic.

Deliverables:

- [ ] Define language-level `Result<T,E>` type semantics.
- [ ] Define construction, tag checks, payload extraction, and unwrap behavior.
- [ ] Define propagation syntax or standard propagation primitive.
- [ ] Map `System.*` expected host failures to result-returning APIs where appropriate.
- [ ] Add diagnostics for unchecked/invalid result use if adopted.
- [ ] Ensure SIR/SBC result-like metadata/opcodes are canonical.
- [ ] Add docs/examples for `Result`-based APIs.

Exit criteria:

- Library and user code can model expected failure without ad hoc sentinel values.

## `v0.7.3` — `Option<T>` and absence handling

Goal: make nullable/optional state explicit without implicit null behavior.

Deliverables:

- [ ] Define language-level `Option<T>` type semantics.
- [ ] Define `some`, `none`, `isSome`, `isNone`, unwrap/default behavior.
- [ ] Use `Option<T>` in Standard APIs where absence is expected.
- [ ] Add pattern/branch helpers or standard utilities if pattern syntax is not ready.
- [ ] Add docs/examples for option use.

Exit criteria:

- User code has a canonical representation for optional values.

## `v0.7.4` — Closures and captured functions

Goal: support first-class behavior without compromising VM/layout clarity.

Deliverables:

- [ ] Define function/procedure value type identity.
- [ ] Define closure capture representation.
- [ ] Define captured local/upvalue metadata.
- [ ] Define capture lifetime/rooting rules.
- [ ] Lower closures through SIR/SBC metadata and bytecode.
- [ ] Add JIT/interpreter parity tests.
- [ ] Add diagnostics for invalid captures and escaping references.

Exit criteria:

- Simple supports closures/lambdas in ordinary collection, async, and callback-style code.

## `v0.7.5` — `Promise<T>` language integration and `await`

Goal: connect v0.6 Promise runtime to the language.

Deliverables:

- [ ] Define language-level `Promise<T>` type semantics.
- [ ] Add `await` expression semantics.
- [ ] Define where `await` may appear.
- [ ] Define cancellation/error propagation behavior for awaited promises.
- [ ] Ensure interpreter/JIT parity.
- [ ] Add diagnostics for awaiting non-promise values.
- [ ] Add docs/examples using process/network/http promises.

Exit criteria:

- User code can await promise-returning library calls idiomatically.

## `v0.7.6` — `async` functions

Goal: make asynchronous user functions first-class.

Deliverables:

- [ ] Define `async` function declaration syntax.
- [ ] Define return type lowering to `Promise<T>`.
- [ ] Define suspension/resume state representation.
- [ ] Define error/cancel propagation through async frames.
- [ ] Define async recursion and reentrancy rules.
- [ ] Add tests for nested awaits, multiple awaits, loops, branches, errors, cancellation, and resource cleanup.

Exit criteria:

- Users can write async Simple functions without manually constructing promises for every operation.

## `v0.7.7` — Resource-safe control flow and diagnostics

Goal: make resources and errors safe at the language level.

Deliverables:

- [ ] Define `defer`/scope-cleanup or equivalent resource-safe pattern.
- [ ] Define behavior across `return`, `break`, `skip`, traps, result propagation, and async suspension.
- [ ] Improve diagnostics for:
  - [ ] ownership misuse
  - [ ] invalid resource escape
  - [ ] missing cleanup where statically knowable
  - [ ] invalid async/resource combinations
- [ ] Add language spec sections for generics, result, option, promise, closures, await, async, and cleanup.

Exit criteria:

- Users can write robust resource-owning code with predictable cleanup semantics.

## `v0.7.0` — Language completion stable

`v0.7.0` can be cut when:

- [ ] generics are complete enough for standard library wrappers;
- [ ] `Result<T,E>` and `Option<T>` are real language/library values;
- [ ] closures are usable;
- [ ] `Promise<T>`, `await`, and `async` are specified and tested;
- [ ] resource-safe control flow is specified;
- [ ] docs and examples show idiomatic language patterns;
- [ ] interpreter and JIT behavior match for supported features;
- [ ] full suite and JIT suite are green.

---

# `v0.8` — Runtime, GC, JIT, AOT, and verifier maturity

Theme:

> Make the VM stronger, safer, faster, and more production-like.

`v0.8` is VM-first. It should not expand the language surface unless required for runtime correctness.

## `v0.8.1` — GC stress, root validation, and heap invariants

Goal: determine whether the custom GC is sufficient and harden it.

Deliverables:

- [ ] Add GC stress mode.
- [ ] Add allocation-at-every-safe-point tests.
- [ ] Validate roots for stack, globals, arrays, strings, closures, promises, channels, handles.
- [ ] Validate native call roots.
- [ ] Validate async/job roots.
- [ ] Add heap invariant checker.
- [ ] Add leak/regression tests.
- [ ] Decide whether to continue custom GC, integrate Boehm, evaluate MMTk, or design a new precise/generational collector.

Exit criteria:

- The current GC has measured correctness coverage and a documented future direction.

## `v0.8.2` — Safepoints, stack maps, and JIT/GC coordination

Goal: make JIT code cooperate with GC and runtime suspension safely.

Deliverables:

- [ ] Define safepoint contract for interpreter and JIT.
- [ ] Emit/record stack maps for JIT frames where required.
- [ ] Define pinned references and keepalive behavior.
- [ ] Validate write/read barrier usage.
- [ ] Add tests for GC during JIT execution and native calls.

Exit criteria:

- JIT execution can participate safely in GC-sensitive workloads.

## `v0.8.3` — Verifier and bytecode hardening

Goal: make SBC validation a strong safety boundary.

Deliverables:

- [ ] Complete wrong resource kind verifier checks for opaque handle use.
- [ ] Add type-extension rows for resource kind, capability tags, blocking behavior, and platform availability.
- [ ] Harden import/native signature validation.
- [ ] Add malformed SBC fuzz tests.
- [ ] Add verifier diagnostics with source/debug context where available.
- [ ] Define SBC version compatibility rules.

Exit criteria:

- Invalid bytecode is rejected before execution with clear diagnostics.

## `v0.8.4` — AOT object emission prototype

Goal: introduce native compilation without replacing SBC as the portable artifact.

Deliverables:

- [ ] Define AOT pipeline: SIR/SBC metadata to LLVM/module/object.
- [ ] Emit object files for a narrow supported subset.
- [ ] Link with Simple runtime.
- [ ] Produce executable or shared library prototype.
- [ ] Preserve runtime metadata required for GC, reflection-lite diagnostics, native calls, and stack traces.
- [ ] Document unsupported features.

Exit criteria:

- A minimal Simple program can be compiled ahead-of-time into a native artifact.

## `v0.8.5` — Profiling, tracing, and runtime diagnostics

Goal: make performance and runtime behavior observable.

Deliverables:

- [ ] Execution statistics for interpreter/JIT/native calls.
- [ ] Allocation and GC statistics.
- [ ] Promise/job/thread/channel diagnostics.
- [ ] Optional trace logging categories.
- [ ] CLI flags for runtime stats and traces.
- [ ] Docs for profiling output.

Exit criteria:

- Users can inspect where time, allocation, native calls, and async waits are spent.

## `v0.8.6` — JIT Tier 1 and performance pass

Goal: move beyond correctness-only JIT for important hot paths.

Deliverables:

- [ ] Identify hot bytecode patterns from benchmarks.
- [ ] Add stable cache/invalidation model.
- [ ] Improve direct calls/import/native lowering.
- [ ] Improve loops, array/list access, arithmetic, strings, and calls.
- [ ] Add performance regression tests/benchmarks.
- [ ] Preserve safe fallback behavior.

Exit criteria:

- JIT has measurable, tracked improvements without correctness regressions.

## `v0.8.0` — Runtime maturity stable

`v0.8.0` can be cut when:

- [ ] GC behavior is stress-tested and documented;
- [ ] JIT/GC coordination is safe for supported features;
- [ ] bytecode verifier is hardened;
- [ ] AOT prototype exists or is explicitly deferred with documented rationale;
- [ ] runtime profiling/tracing exists;
- [ ] performance has baseline benchmarks;
- [ ] full suite and JIT suite are green.

---

# `v0.9` — Tooling, packages, distribution, and ecosystem usability

Theme:

> Make Simple pleasant to install, edit, build, test, package, and distribute outside the repository.

`v0.9` is user-workflow-first.

## `v0.9.1` — Project manifest and workspace model

Goal: standardize project layout.

Deliverables:

- [ ] Define `simple.toml` or equivalent manifest.
- [ ] Define project name/version/entrypoint/module roots.
- [ ] Define build profiles.
- [ ] Define dependency declarations.
- [ ] Define test targets.
- [ ] Add CLI support for project discovery.

Exit criteria:

- `svm` can understand a Simple project without ad hoc command arguments.

## `v0.9.2` — Package/dependency manager

Goal: make code reuse practical.

Deliverables:

- [ ] Define package identity and version constraints.
- [ ] Define lockfile format.
- [ ] Implement local/path dependencies.
- [ ] Implement git dependencies or registry stub.
- [ ] Add dependency resolver tests.
- [ ] Define package cache layout.

Exit criteria:

- A project can depend on another Simple package reproducibly.

## `v0.9.3` — Formatter

Goal: provide canonical code formatting.

Deliverables:

- [ ] Define formatting style.
- [ ] Implement formatter.
- [ ] Add idempotence tests.
- [ ] Add CLI command.
- [ ] Add docs/editor integration notes.

Exit criteria:

- `svm fmt` or equivalent formats Simple source consistently.

## `v0.9.4` — Linter and static checks

Goal: catch common mistakes before runtime.

Deliverables:

- [ ] Define lint rule framework.
- [ ] Add initial rules for unused imports, unreachable code, suspicious resource handling, redundant casts, and deprecated APIs.
- [ ] Add configuration support.
- [ ] Add CLI command.
- [ ] Add docs.

Exit criteria:

- Users can run static quality checks separately from compilation.

## `v0.9.5` — LSP polish

Goal: make editor experience solid.

Deliverables:

- [ ] Completion quality pass.
- [ ] Hover/spec doc integration.
- [ ] Signature help for user and library functions.
- [ ] Go-to definition.
- [ ] Find references.
- [ ] Rename where safe.
- [ ] Diagnostics with fix suggestions where feasible.
- [ ] Semantic token coverage.

Exit criteria:

- Editing Simple in supported editors feels coherent and accurate.

## `v0.9.6` — Debugger/profiler CLI

Goal: make runtime issues easier to inspect.

Deliverables:

- [ ] Source maps/debug metadata consumption.
- [ ] Stack traces with file/line/function.
- [ ] Breakpoint or tracepoint prototype.
- [ ] Runtime stats integration from v0.8.
- [ ] Error report formatting.

Exit criteria:

- Users can diagnose failures without reading raw VM internals.

## `v0.9.7` — Documentation generator

Goal: make library/package docs reproducible.

Deliverables:

- [ ] Generate API docs from catalog/user source metadata.
- [ ] Support examples in docs.
- [ ] Validate docs examples compile.
- [ ] Website integration.

Exit criteria:

- Public docs can be regenerated and checked for drift.

## `v0.9.8` — Installers, templates, and distribution

Goal: make installation and project creation easy.

Deliverables:

- [ ] Cross-platform installers or archive packages.
- [ ] Version manager update flow.
- [ ] `svm new` project templates.
- [ ] CI templates/examples.
- [ ] Release checks for Linux/macOS/Windows.

Exit criteria:

- New users can install Simple, create a project, run tests, and build without repository-specific knowledge.

## `v0.9.0` — Ecosystem usability stable

`v0.9.0` can be cut when:

- [ ] project manifests are stable enough for real projects;
- [ ] packages/dependencies work reproducibly;
- [ ] formatter and linter exist;
- [ ] LSP is useful and accurate;
- [ ] debugger/profiler basics exist;
- [ ] docs generation avoids drift;
- [ ] install/project templates work cross-platform;
- [ ] full suite and release workflow are green.

---

# `v1.0` — Stability contract

Theme:

> Freeze what users can depend on.

`v1.0` is not a feature release. It is the compatibility and documentation release.

## `v1.0.0` release contract

Language stability:

- [ ] Stable syntax for all non-experimental language features.
- [ ] Stable type system rules.
- [ ] Stable generics, closures, `Result`, `Option`, `Promise`, `await`, `async`, and cleanup semantics if shipped before v1.0.
- [ ] Complete language reference.
- [ ] Clear experimental-feature policy.

Library stability:

- [ ] Stable public `System.*` modules shipped for v1.0.
- [ ] Stable public `Standard.*` modules shipped for v1.0.
- [ ] No accidental aliases or duplicate roots.
- [ ] Deprecation and migration policy documented.
- [ ] Capability/security behavior documented.

Runtime/SBC stability:

- [ ] SBC versioning policy.
- [ ] Runtime compatibility policy.
- [ ] Interpreter/JIT semantic parity policy.
- [ ] Native ABI/catalog compatibility policy.
- [ ] Resource lifetime and shutdown policy.
- [ ] GC behavior and limitations documented.

Tooling stability:

- [ ] CLI command contract documented.
- [ ] Manifest/package format contract documented.
- [ ] Formatter stability policy.
- [ ] LSP support level documented.
- [ ] Release/install/update flow documented.

Quality gates:

- [ ] Full suite green.
- [ ] JIT suite green.
- [ ] Release workflow green on Linux/macOS/Windows.
- [ ] Website/docs examples compile or are explicitly marked WIP/experimental.
- [ ] Conformance tests exist for language/library/runtime contracts.
- [ ] Security/capability test coverage exists for dangerous host operations.

Exit criteria:

- A user can install Simple, create a project, add dependencies, write scripts/tools with native APIs, use async/concurrency, build/package, and rely on documented compatibility guarantees.

---

# Post-v1.0 candidates

These are important but do not block v1.0 unless pulled forward deliberately:

- Production-grade HTTP server framework.
- HTTPS/TLS full support if not completed before v1.0.
- UDP and advanced socket options.
- Major GC replacement or MMTk/Boehm migration if custom GC remains acceptable through v1.0.
- Advanced work-stealing scheduler.
- Advanced optimizing JIT tiers.
- Full native AOT packaging if only prototype status by v1.0.
- WebAssembly target.
- Foreign module/package registry service.
- Full terminal UI framework.
- `System.ASM` / native-code-generation pipeline.

---

# v0.6 audit reference

Detailed domain audit: [`LibraryDomainAudit_v0.6.md`](LibraryDomainAudit_v0.6.md).

That audit should be updated as each `v0.5.x` slice lands.
