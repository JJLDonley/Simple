# Simple JIT

`Simple::VM::Jit` owns tiering policy, JIT counters, compiled-runner scaffolding, and compiled-path failure formatting. The interpreter remains the correctness baseline.

## Owned files

- JIT public/runtime constants: `VM/include/vm.h`
- Tier updater: `VM/include/jit/tier_updater.h`, `VM/src/jit/tier_updater.cpp`
- Compile policy: `VM/include/jit/compile_policy.h`, `VM/src/jit/compile_policy.cpp`
- Compiled runner: `VM/include/jit/compiled_runner.h`, `VM/src/jit/compiled_runner.cpp`
- Failure formatting: `VM/include/jit/failure_format.h`, `VM/src/jit/failure_format.cpp`
- Scaffold classification: `VM/include/jit/jit_scaffold.h`, `VM/src/jit/jit_scaffold.cpp`

## Runtime contract

- JIT is optional and controlled through `ExecuteModule(..., enable_jit, options)`.
- `ExecOptions::force_interpreter` keeps execution on the interpreter path.
- Tier counters are exposed through `ExecResult` for tests and diagnostics.
- Hot functions may move to Tier0/Tier1 according to call/opcode thresholds.
- Unsupported compiled-path behavior must fail safely or fall back; it must not change interpreter correctness.

## Tiering and policy

Tiering state uses `JitTier`, call counts, opcode counts, dispatch counts, compiled execution counts, and Tier1 execution counts. `CompilePredicate` and `CanCompileMethod` decide whether a method is eligible for the compiled runner.

## Compiled runner

`RunCompiledFunction` executes the supported compiled subset using `CompiledRunContext`. It owns the former VM-local compiled-runner lambda and handles recursive compiled calls through the same context.

Failure details are formatted by `FormatCompiledFailure` / `CompiledFailureReporter`, including opcode, pc, call operands, jump operands, and jump-table operands where available.

## Forbidden dependencies

- JIT helpers must not live in `VM/src/vm.cpp` as lambdas/helpers.
- JIT must not own native stdlib, FFI/DL, CLI, or LSP behavior.
- JIT must preserve interpreter semantics and runtime limits.

## Tests

JIT coverage lives in:

- `Tests/tests/test_jit.cpp`
- `Tests/tests/vm/test_jit.cpp`
- JIT ownership guards in `Tests/tests/vm/test_interpreter.cpp`
