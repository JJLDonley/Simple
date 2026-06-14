# Simple Project Coding Standards

These standards are mandatory for all new code and refactors in this repository.

## 1. Priorities

1. Correctness and testability first.
2. Clear module boundaries before new feature growth.
3. Keep the interpreter and compiler phase behavior stable.
4. Prefer small, reviewable changes.

## 2. C++ Baseline

- Use C++17.
- Use the standard library unless project-specific behavior is required.
- Avoid exceptions in VM/compiler hot paths.
- Avoid RTTI-dependent designs.
- Keep public headers minimal.

## 3. Formatting

- 2-space indentation.
- No tabs.
- Keep lines reasonably short, target about 100 columns.
- Braces stay attached to declarations/statements.
- Prefer early returns over deeply nested code.

## 4. Naming

- Types/classes/structs/enums: `PascalCase`.
- Public functions: `PascalCase`.
- Local variables and lambdas: `snake_case`.
- Constants: `kPascalCase`.
- Enum values: `PascalCase`.
- File names should describe their module responsibility.

## 5. Single Responsibility Principle

Every file/module must have one clear responsibility.

Do not add unrelated behavior to large monoliths.

Primary monoliths/offenders, in priority order:

1. `VM/src/vm.cpp` — split first.
2. `Lang/src/lang_validate.cpp` — split second.
3. `Tests/tests/test_lang.cpp` — split after phase boundaries stabilize.
4. `Tests/tests/test_core.cpp` — split by VM subsystem.
5. `CLI/src/main.cpp` — split diagnostics/import/build helpers after shared import graph extraction.

Required SRP refactor order:

1. Split VM native/runtime boundaries.
2. Split language validation boundaries.
3. Split tests by subsystem/phase.
4. Split CLI/import/diagnostic services.
5. Only then add large features like Thread jobs, Net, and Http.

End-state rule: this refactor must not leave permanent shims, compatibility facades, facade-only modules, or forwarding wrappers. Temporary facades are allowed only inside an active migration step and must be removed before the refactor is considered complete.

New VM runtime features must move toward dedicated modules:

- `VM/src/interpreter/interpreter.cpp`
- `VM/src/interpreter/dispatch.cpp`
- `VM/src/interpreter/frames.cpp`
- `VM/src/interpreter/stack.cpp`
- `VM/src/native/registry.cpp`
- `VM/src/native/fs.cpp`
- `VM/src/native/channel.cpp`
- `VM/src/native/buffer.cpp`
- `VM/src/native/json.cpp`
- `VM/src/ffi/dl_runtime.cpp`
- `VM/src/jit/jit_scaffold.cpp`
- `VM/src/gc/root_tracer.cpp`
- `VM/src/runtime/runtime_limits.cpp`

VM boundary types should be explicit and named:

- `NativeCallContext`
- `NativeCallResult`
- `NativeModule`
- `NativeFunction`
- `FrameState`
- `InterpreterState`
- `RootTraceContext`

Interpreter modules own opcode loop, stack operations, frames, locals/globals, calls/tailcalls, and traps. They must not own native stdlib implementation, DL/FFI internals, JSON parsing, channel registries, or platform FS code.

New language features should respect phase boundaries:

- `Lexer`
- `CAST`
- `AST`
- `RAST`
- `TAST`
- `IRB`
- `IRE`

## 6. Language Include Policy

New code should include phase headers directly.

Prefer:

- `CAST/parser.h` over `lang_parser.h`
- `AST/ast.h` over `lang_ast.h`
- `RAST/resolver.h` and `TAST/type_checker.h` over `lang_validate.h`
- `IRE/sir_emitter.h` over `lang_sir.h`

Legacy `lang_*.h` headers are temporary compatibility facades only. The final SRP end state must remove these headers after callers migrate to phase headers; do not design new code around them.

## 7. Error Handling

Compiler/language phases:

- Return `bool` for success/failure.
- Use `std::string* error` for diagnostic text.
- Preserve useful source location information.

VM/runtime:

- Use `ExecResult` and trap messages for runtime failures.
- Do not silently ignore invalid bytecode or invalid native calls.

CLI:

- Diagnostics must use stable `error[Exxxx]:` prefixes.
- Follow code ranges documented in `Docs/CLI.md`.

## 8. Native Runtime Bindings

Native stdlib functions must have:

- Explicit argument count checks.
- Explicit argument type checks.
- Explicit return type checks.
- Stable diagnostics for invalid calls.
- Tests covering success and failure behavior where practical.

New native modules should not be implemented as ad hoc growth inside `VM/src/vm.cpp`.

Native bindings should move toward metadata-driven dispatch with:

- `NativeFunctionSpec`
- module name
- symbol name
- parameter types
- result type
- named handler function

The same metadata should eventually drive VM dispatch, Lang reserved signatures, and stdlib documentation generation.

## 9. Tests Required

Every behavior change must include tests.

Recommended locations:

- VM/runtime behavior: `Tests/tests/test_core.cpp` or future VM-specific split.
- Language parser/semantic behavior: language phase tests or `Tests/tests/test_lang.cpp` until split.
- CLI behavior: command-level tests in `Tests/tests/test_lang.cpp`.
- Positive fixtures: `Tests/simple/*.simple`.
- Negative fixtures: `Tests/simple_bad/*.simple`.

Tests should be deterministic and not depend on external network services.

## 10. Documentation Required

Update documentation when behavior changes affect users, embedders, or compatibility.

Relevant docs:

- `Docs/TODO.md`
- `Docs/CLI.md`
- `Docs/StdLib.md`
- `Docs/VM.md`
- `Docs/Compatibility.md`
- `Docs/Language.md`

TODO entries must be actionable.

## 11. Compatibility

Respect public compatibility surfaces:

- Lang syntax version
- SIR version
- SBC version
- Opcode metadata version
- Runtime ABI version
- Stdlib version

Breaking changes require explicit documentation and a versioning decision.

## 12. Code Review Checklist

Before committing:

- Code follows SRP.
- No unnecessary growth in known monoliths.
- Public behavior is tested.
- Diagnostics are stable and useful.
- Docs/TODOs are updated when needed.
- Full test suite passes.
