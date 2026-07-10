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

The `-jit` path is LLVM ORC first: it attempts to execute supported SBC functions as native LLVM code and falls back to the interpreter for unsupported shapes. The old tiered compiled-runner implementation has been removed.

The LLVM ORC backend is present behind CMake option `SIMPLEVM_ENABLE_LLVM_JIT`. This is the migration path toward JVM/CLR-style execution:

```text
Simple -> SIR -> SBC -> verifier -> SBC-to-LLVM lowering -> ORC JIT -> Simple runtime helpers
```

When enabled and completed, `.sbc` remains the portable artifact while the platform-specific `svm` runtime JIT-compiles functions to native code on the user's machine.

## Enabling JIT

JIT behavior is controlled through the VM execution API:

```cpp
ExecuteModule(module, verify, enable_jit, options)
```

`ExecOptions::force_interpreter` disables compiled-path execution even when JIT is otherwise enabled.

## Tiering

The VM records per-function call/opcode/dispatch/native-exec counters for diagnostics. Some result fields still use legacy `tier` names while the stats ABI is cleaned up, but execution no longer routes through the old tiered compiled runner.

## Eligibility

Not every method can run through LLVM yet. Unsupported methods stay on or fall back to the interpreter path.

## LLVM ORC migration

Build-time switch:

```bash
cmake -S Compiler -B Compiler/build-llvm -DSIMPLEVM_ENABLE_LLVM_JIT=ON
```

Runtime selection during the migration:

```bash
svm run -jit main.simple     # use JIT paths where supported
svm run -int main.simple     # force interpreter
```

Until the LLVM JIT is complete, CLI execution defaults to interpreter mode and `-jit` opts in. Later this should flip so JIT is default and `-int` is the fallback/debug mode.

The ORC backend caches generated entries by module/function/code hash and verifies generated IR before installation. Current LLVM lowering covers scalar functions with constants including validated 128-bit placeholder constants, locals, uninitialized and numeric f32/f64-initialized globals in non-calling functions, integer/unsigned/floating arithmetic, checked i32/u32/i64/u64 arithmetic/div/mod, checked integer/float scalar conversions, checked/guard bounds, simple pointer guards/comparisons and pseudo-memory, trap/intrinsic-trap/syscall/throw/panic fallback, address/capture-local/global, algebraic wrapper, iterator/task placeholder, atomic placeholder, vector placeholder ops, comparisons, simple conversions, forward branches, `JmpTable`, loop state merging for validated scalar/ref-stack cases, scalar-safe direct Simple calls inside loops, scalar/void helper-bridged import calls inside loops, `Yield`, selected intrinsics, self calls, no-capture function literals via `NewClosure` + `CallIndirect`, and helper-bridged direct/import/native/indirect non-self calls. Native import calls inside loops require scalar/void signatures plus safe metadata: matching signature, non-blocking, no allocation, no GC safepoint, and no resources. Dynamic `System.dl.call$...` loop calls must also pass the external C ABI verifier before LLVM accepts the loop. Ref/string/resource/allocating direct/import calls and indirect/procedure calls inside loops are still conservatively rejected until full caller-frame root publication, exact target metadata, and safepoint state are hardened. `--jit-stats` prints function names with indexes and rejection reasons include opcode/pc plus call category/safety reason where available, so mixed workloads, such as raylib loops, can identify the exact hot function and bytecode operation that fell back.

Remaining migration order:

1. add heap-aware aggregate/runtime trap helpers
2. replace helper-bridged calls with true multi-function LLVM module emission
3. route arrays/lists/strings/globals/imports through C ABI runtime helpers
4. add GC safepoints/root maps
5. keep interpreter parity tests until full coverage is reached

## LLVM ORC backend

New JIT work targets `LlvmJitBackend` and interpreter fallback only. The removed compiled runner must not be reintroduced as a compatibility path. `JitCallContext` is the stable internal ABI for compiled Simple functions: it carries args, operand stack slots, locals, spills, globals, heap/runtime access, return slot, trap state, and explicit root refs. LLVM helper dispatch now uses this context as the correctness ABI before returning to compiled code. Helper entry publishes typed callee argument roots, captures caller local/operand-stack snapshots supplied by compiled call sites, and marks helper-call safepoint metadata with caller function/pc plus blocking/allocation flags derived from native/import metadata where available. Parameter-local root masks are published before dispatch while exact stack/local maps continue to expand. LLVM ORC cache keys include module identity, function index, code range/hash, JIT cache ABI version, runtime helper ABI version, and whether runtime helpers are used.

## Failure reporting

Compiled-path failures include opcode, program counter, and decoded operand context where possible. The error should make it clear which opcode and location failed without corrupting VM state. `JitStatusCode` classifies outcomes as `halt`, `return`, `trap`, `fallback`, or `unsupported` so callers can distinguish semantic exits from safe interpreter fallback. `ExecResult::jit_status_counts` aggregates compiled returns and classified LLVM rejection/fallback reasons by status-code ordinal.

## Correctness rule

The JIT must never define different language semantics from the interpreter. If a behavior is not supported by the compiled path, it must fail safely or use interpreter execution.

## Tests

JIT behavior is covered by LLVM ORC tests in `Tests/tests/test_jit.cpp`, `Tests/tests/vm/test_jit.cpp`, and VM integration tests that inspect JIT counters, reject reasons, and interpreter fallback.
