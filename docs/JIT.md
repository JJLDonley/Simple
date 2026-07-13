# Simple JIT

The Simple JIT is an optional execution path layered on top of the VM. The interpreter remains the correctness baseline.

## Table of contents

- [Status](#status)
- [Enabling JIT](#enabling-jit)
- [Tiering](#tiering)
- [Eligibility](#eligibility)
- [LLVM ORC backend](#llvm-orc-backend)
- [Failure reporting](#failure-reporting)
- [Correctness rule](#correctness-rule)
- [Tests](#tests)

## Status

The LLVM ORC JIT path is stable for the current explicit JIT suite. It attempts to execute supported SBC functions as native LLVM code and falls back to the interpreter for unsupported shapes. The old tiered compiled-runner implementation has been removed.

The LLVM ORC backend is present behind CMake option `SIMPLEVM_ENABLE_LLVM_JIT`. This is the migration path toward JVM/CLR-style execution:

```text
Simple -> SIR -> SBC -> verifier -> SBC-to-LLVM lowering -> ORC JIT -> Simple runtime helpers
```

When enabled and completed, `.sbc` remains the portable artifact while the platform-specific `svm` runtime JIT-compiles functions to native code on the user's machine.

## Enabling JIT

Official LLVM packages and CI builds target LLVM 18. Releases use runtime flavor classifiers
rather than separate version tags:

| Operating system | Interpreter (`int`) | LLVM ORC (`llvm`) |
|---|:---:|:---:|
| Linux x86_64 | ✅ | ✅ |
| macOS hosted-runner architecture | ✅ | ✅ |
| Windows x86_64 | ✅ | 🧪 Published when its experimental build succeeds |

For example, `simple-v0.5.0-linux-x86_64-int.tar.gz` and
`simple-v0.5.0-linux-x86_64-llvm.tar.gz` belong to the same `v0.5.0` release. The suffix is an
artifact flavor, not a SemVer prerelease suffix. Release CI builds and tests each published flavor.
The interpreter package remains the dependency-light release flavor. Linux interpreter and LLVM
packages are required release outputs. macOS and Windows jobs remain allowed-to-fail, but every
successful flavor is uploaded. Windows LLVM 18 is therefore experimental and opportunistic rather
than a release gate.

The Unix source installer enables LLVM JIT by default and accepts `--no-llvm-jit` for an
interpreter-only install. CLI execution uses JIT by default when `svm` is built with LLVM ORC support. Use `-int` or `--interpreter` to force the interpreter. The compatibility `-jit` flag is still accepted.

Library/API execution remains interpreter-first unless the caller explicitly opts in:

```cpp
ExecuteModule(module, verify, enable_jit, options)
```

`ExecOptions::force_interpreter` disables compiled-path execution even when JIT is otherwise enabled.

## Tiering

The VM records per-function call/opcode/dispatch/native-exec counters for diagnostics. Some result fields still use legacy `tier` names while the stats ABI is cleaned up, but execution no longer routes through the old tiered compiled runner.

## Eligibility

Not every method can run through LLVM yet. Unsupported methods stay on or fall back to the interpreter path.

## LLVM ORC migration

The Unix installer enables JIT by default:

```bash
./scripts/install/unix.sh
```

Use `./scripts/install/unix.sh --no-llvm-jit` to opt out. For manual CMake builds, enable the backend explicitly:

```bash
cmake -S . -B build -DSIMPLEVM_ENABLE_LLVM_JIT=ON
```

Runtime selection during the migration:

```bash
svm run main.simple          # use JIT paths where supported
svm run main.simple -int     # force interpreter
svm run main.simple -jit     # accepted compatibility spelling
```

The ORC backend caches generated entries by module/function/code hash and verifies generated IR before installation. Current LLVM lowering covers scalar functions with constants including validated 128-bit placeholder constants, locals, uninitialized and numeric f32/f64-initialized globals in non-calling functions, integer/unsigned/floating arithmetic, checked i32/u32/i64/u64 arithmetic/div/mod, checked integer/float scalar conversions, checked/guard bounds, simple pointer guards/comparisons and pseudo-memory, trap/intrinsic-trap/syscall/throw/panic fallback, address/capture-local/global, algebraic wrapper, iterator/task placeholder, atomic placeholder, vector placeholder ops, comparisons, simple conversions, forward branches, `JmpTable`, loop state merging for validated scalar/ref-stack cases, scalar-safe direct Simple calls inside loops, including void methods encoded as unspecified returns, scalar/void helper-bridged import calls inside loops, `Yield`, selected intrinsics, self calls, no-capture function literals via `NewClosure` + `CallIndirect`, and helper-bridged direct/import/native/indirect non-self calls. Loop-call safety diagnostics are scoped to calls that are actually inside detected backward-branch ranges, so pre-loop setup imports no longer block render/update loop lowering. Native import calls inside loops require safe metadata: matching signature, non-blocking, no allocation, no GC safepoint, and no output/mutating resources; managed string/ref arguments and borrowed resource inputs are allowed through the helper ABI when those metadata checks pass. Dynamic `System.FFI.call$...` loop calls must pass the canonical VM ABI/native ABI FFI verifier before LLVM loop-call acceptance, and then remain scalar/void or borrowed C-string input calls before LLVM accepts the helper path; accepted scalar/string-input dynamic-DL calls use generic VM ABI/native ABI helper dispatch through `JitCallContext` caller snapshots/root publication. Library-specific dynamic-DL direct binds are intentionally not part of LLVM lowering; aggregate external-C calls remain behind the canonical VM ABI/native ABI marshaler until generic aggregate support is proven. The LLVM cache/helper ABI versions are part of the cache key and are bumped when helper signatures or JIT acceptance rules change. Loop-call rejection diagnostics include the opcode PC, call category, target label, SBC signature shape, and aggregate field layout fingerprints so remaining FFI blockers can be mapped to concrete ABI forms. `System.Job` and `Standard.Promise` imports are interpreter-only in `v0.5.2` so each worker and promise remains owned by the executing VM's resource registry; LLVM falls back before creating async state. Resource/allocating direct/import calls, managed-result imports, managed direct calls, and indirect/procedure calls inside loops are still conservatively rejected until full caller-frame root publication, exact target metadata, and safepoint state are hardened. `--jit-stats` prints function names with indexes and rejection reasons include opcode/pc plus call category/safety reason and target labels where available, so mixed workloads, such as raylib loops, can identify the exact hot function, callee, and bytecode operation that fell back.

Remaining migration order:

1. add heap-aware aggregate/runtime trap helpers
2. replace helper-bridged calls with true multi-function LLVM module emission
3. route arrays/lists/strings/globals/imports through C ABI runtime helpers
4. add GC safepoints/root maps
5. keep interpreter parity tests until full coverage is reached

## LLVM ORC backend

New JIT work targets `LlvmJitBackend` and interpreter fallback only. The removed compiled runner must not be reintroduced as a compatibility path. `JitCallContext` is the stable internal ABI for compiled Simple functions: it carries args, operand stack slots, locals, spills, globals, heap/runtime access, return slot, trap state, and explicit root refs. LLVM helper dispatch now uses this context as the correctness ABI before returning to compiled code. Helper entry publishes typed callee argument roots, captures caller local/operand-stack snapshots supplied by compiled call sites, and marks helper-call safepoint metadata with caller function/pc plus blocking/allocation flags derived from native/import metadata where available. Exact known local and operand-stack root masks are published before dispatch, with branch merges preserving root facts only when incoming slot types agree. Published JIT helper roots are registered as thread-local GC roots while helper/interpreter/native dispatch is active. LLVM ORC cache keys include module identity, function index, code range/hash, JIT cache ABI version, runtime helper ABI version, and whether runtime helpers are used.

## Failure reporting

Compiled-path failures include opcode, program counter, and decoded operand context where possible. The error should make it clear which opcode and location failed without corrupting VM state. `JitStatusCode` classifies outcomes as `halt`, `return`, `trap`, `fallback`, or `unsupported` so callers can distinguish semantic exits from safe interpreter fallback. `ExecResult::jit_status_counts` aggregates compiled returns and classified LLVM rejection/fallback reasons by status-code ordinal.

## Correctness rule

The JIT must never define different language semantics from the interpreter. If a behavior is not supported by the compiled path, it must fail safely or use interpreter execution.

## Tests

JIT behavior is covered by LLVM ORC tests in `tests/tests/test_jit.cpp`, `tests/tests/vm/test_jit.cpp`, and VM integration tests that inspect JIT counters, reject reasons, and interpreter fallback. The current explicit JIT section is green at `61/61` tests.
