# Simple JIT

The Simple JIT is an optional execution path layered on top of the VM. The interpreter remains the correctness baseline.

## Status

The current JIT is a tiering and compiled-runner scaffold, not a full native-code compiler. It tracks hot functions, classifies eligibility, executes a supported compiled subset, and falls back or reports safe failures for unsupported paths.

## Enabling JIT

JIT behavior is controlled through the VM execution API:

```cpp
ExecuteModule(module, verify, enable_jit, options)
```

`ExecOptions::force_interpreter` disables compiled-path execution even when JIT is otherwise enabled.

## Tiering

The VM records per-function call counts and opcode counts. When thresholds are reached, functions may move through JIT tiers:

- baseline/interpreter state
- Tier0 for opcode-hot or initially compiled candidates
- Tier1 for hotter call-count driven candidates

`ExecResult` exposes tier and counter vectors so tests and diagnostics can inspect what happened.

## Eligibility

Not every method can run through the compiled runner. The compile policy checks method shape, bytecode features, and supported opcode subsets before allowing compiled execution.

Unsupported methods stay on or fall back to the interpreter path.

## Compiled runner

The compiled runner executes a supported bytecode subset in a JIT-owned path. It supports scalar arithmetic, locals, selected control flow, direct/indirect/tail calls where eligible, and the other opcode behavior covered by JIT tests.

Recursive compiled calls reuse the same compiled-runner context.

## Failure reporting

Compiled-path failures include opcode, program counter, and decoded operand context where possible. The error should make it clear which opcode and location failed without corrupting VM state.

## Correctness rule

The JIT must never define different language semantics from the interpreter. If a behavior is not supported by the compiled path, it must fail safely or use interpreter execution.

## Tests

JIT behavior is covered by `Tests/tests/test_jit.cpp`, `Tests/tests/vm/test_jit.cpp`, and VM integration tests that inspect tier counters and compiled execution counters.
