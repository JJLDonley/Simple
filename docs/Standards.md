# Simple Project Coding Standards

These are standing repository rules, not a one-time cleanup plan. They apply to every feature, fix, refactor, build change, test change, and documentation update. Each minor update must finish with the cleanup gate in Section 13 before it is committed.

## 1. Priorities

1. Correctness and testability first.
2. Clear module boundaries before new feature growth.
3. Keep the interpreter and compiler phase behavior stable.
4. Prefer small, reviewable changes.

## 2. Modern C++ Rules

Simple uses C++17. New code and touched code must follow modern C++ ownership, lifetime, and type-safety practices rather than C-style or legacy C++ patterns.

Required:

- Use RAII for files, handles, locks, temporary directories, and other resources.
- Express ownership explicitly. Prefer values and references; use `std::unique_ptr` for unique heap ownership and `std::shared_ptr` only when ownership is genuinely shared.
- Do not add owning raw pointers, manual `new`/`delete`, or cleanup that depends on every return path being remembered.
- Prefer `enum class`, typed IDs, `constexpr`, `std::array`, standard containers, and standard algorithms over magic integers, unscoped flags, C arrays, and handwritten container machinery.
- Use `const` and references to make mutation and copying explicit. Use `std::move` only when ownership is actually transferred.
- Use `std::string_view` for non-owning text when the referenced lifetime is unambiguous; use `std::string` when ownership is required.
- Use `std::filesystem` and the project platform interface for paths and host behavior.
- Keep platform APIs, OS headers, and OS conditionals inside `source/Platform` implementations. Portable compiler, language, CLI, and VM code must call the platform interface.
- Keep public headers minimal and include what they use.
- Prefer small named functions and types over macros. Preprocessor conditionals are for build/platform boundaries, not ordinary control flow.

Constraints:

- Use the standard library unless project-specific behavior is required.
- Avoid exceptions in VM/compiler hot paths; use the project result/error conventions below.
- Avoid RTTI-dependent designs.
- Do not apply a modernizing rewrite merely for style. Preserve behavior, keep the diff reviewable, and validate it.

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

1. `source/VM/src/vm.cpp` — split first.
2. `source/Lang/src/lang_validate.cpp` — split second.
3. `tests/tests/test_lang.cpp` — split after phase boundaries stabilize.
4. `tests/tests/test_core.cpp` — split by VM subsystem.
5. `source/CLI/src/main.cpp` — split diagnostics/import/build helpers after shared import graph extraction.

Required SRP refactor order:

1. Split VM native/runtime boundaries.
2. Split language validation boundaries.
3. Split tests by subsystem/phase.
4. Split CLI/import/diagnostic services.
5. Only then add large features like Thread jobs, Net, and Http.

A refactor is incomplete if it leaves shims, compatibility facades, facade-only modules, forwarding wrappers, duplicate entry points, or old and new APIs in parallel. Do not add temporary shims. Update all callers in the same change and delete the superseded path.

Platform build policy belongs under `cmake/platform/` and `scripts/build/`. Do not recreate root build/install wrappers. Do not fork compiler or VM implementations by operating system; platform-specific C++ belongs behind `source/Platform`.

New VM runtime features must move toward dedicated modules:

- `source/VM/src/interpreter/interpreter.cpp`
- `source/VM/src/interpreter/dispatch.cpp`
- `source/VM/src/interpreter/frames.cpp`
- `source/VM/src/interpreter/stack.cpp`
- `source/VM/src/native/default_registry.cpp`
- `source/VM/src/native/registry_core.cpp`
- `source/VM/src/native/system_fs.cpp`
- `source/VM/src/native/system_channel.cpp`
- `source/VM/src/native/system_buffer.cpp`
- `source/VM/src/native/system_json.cpp`
- `source/VM/src/ffi/dl_runtime.cpp`
- `source/VM/src/jit/jit_scaffold.cpp`
- `source/VM/src/gc/root_tracer.cpp`
- `source/VM/src/runtime/runtime_limits.cpp`

VM boundary types should be explicit and named:

- `NativeCallContext`
- `NativeCallResult`
- `NativeModule`
- `NativeFunction`
- `FrameState`
- `InterpreterState`
- `RootTraceContext`

Interpreter modules own opcode loop, stack operations, frames, locals/globals, calls/tailcalls, and traps. They must not own native stdlib implementation, System.FFI internals, JSON parsing, channel registries, or platform FS code.

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

Removed aggregate `lang_*.h` compatibility facades must not be recreated. The remaining domain headers such as `lang_library.h`, `lang_reserved.h`, and `lang_version.h` have specific responsibilities and are not aggregate phase facades.

## 7. No Shims or Dead Code

The repository keeps one current implementation of each behavior.

Prohibited:

- compatibility shims, aliases, forwarding wrappers, and fallback implementations retained only for old internal callers;
- unused functions, types, declarations, includes, source files, CMake targets/options, scripts, fixtures, generated binaries, and commented-out implementations;
- placeholder branches or APIs with no immediate caller;
- duplicate helpers with different names;
- production code added only to make a test pass when the intended boundary already exists;
- checked-in build output outside the documented generated directories.

Rules:

- A shared helper must replace at least two real call sites immediately, unless it is a required platform or public boundary.
- When replacing an API, migrate every caller and remove the old API in the same update.
- When diagnostics are temporarily added to investigate CI, remove them after the defect is proven fixed.
- Tests may contain host-specific launch support, but production compatibility behavior must not leak into tests or vice versa.
- Use compiler warnings, repository searches, the audit script, and review of the final diff to prove code is still used. Do not assume successful linking proves the absence of dead code.

## 8. Error Handling

Compiler/language phases:

- Return `bool` for success/failure.
- Use `std::string* error` for diagnostic text.
- Preserve useful source location information.

VM/runtime:

- Use `ExecResult` and trap messages for runtime failures.
- Do not silently ignore invalid bytecode or invalid native calls.

CLI:

- Diagnostics must use stable `error[Exxxx]:` prefixes.
- Follow code ranges documented in `docs/CLI.md`.

## 9. Native Runtime Bindings

Native stdlib functions must have:

- Explicit argument count checks.
- Explicit argument type checks.
- Explicit return type checks.
- Stable diagnostics for invalid calls.
- Tests covering success and failure behavior where practical.

New native modules should not be implemented as ad hoc growth inside `source/VM/src/vm.cpp`.

Native bindings should move toward metadata-driven dispatch with:

- `NativeFunctionSpec`
- module name
- symbol name
- parameter types
- result type
- named handler function

The same metadata should eventually drive VM dispatch, Lang reserved signatures, and stdlib documentation generation.

## 10. Tests Required

Every behavior change must include tests.

Required locations by responsibility:

- VM/runtime behavior: `tests/tests/vm/`; use `tests/tests/test_core.cpp` only for cross-subsystem integration coverage that has no narrower owner.
- Language parser/semantic behavior: `tests/tests/lang/`; use `tests/tests/test_lang.cpp` only for cross-phase language integration.
- CLI behavior: `tests/tests/cli/`.
- LSP behavior: `tests/tests/test_lsp.cpp` until its existing suite is split by protocol area.
- Positive fixtures: `tests/simple/*.simple` and focused module fixtures under `tests/simple_modules/`.
- Negative fixtures: `tests/simple_bad/*.simple`.

Tests should be deterministic and not depend on external network services.

## 11. Documentation Required

Update documentation when behavior changes affect users, embedders, or compatibility.

The public documentation index is `docs/README.md`. Update the relevant
language, CLI, bytecode, VM, JIT, or portability document alongside the code.

## 12. Compatibility

Respect public compatibility surfaces:

- Lang syntax version
- SIR version
- SBC version
- Opcode metadata version
- Runtime ABI version
- Stdlib version

Breaking changes require explicit documentation and a versioning decision.

Release channels before `v1.0` are encoded in the patch component:

- `v0.X.0` is a stable milestone release.
- `v0.X.Y` with `Y > 0` is an experimental development release toward the next stable milestone.
- Experimental versions must be published as GitHub prereleases and must not replace stable `simple-latest-*` packages.
- vcpkg dependency-manifest versions are cache metadata, not the Simple tool version; keep them stable unless dependencies change.

## 13. Continuous Cleanup Gate

Cleanup is mandatory after every minor update. A minor update is one coherent, commit-sized change: a bug fix, small feature slice, refactor slice, build/CI adjustment, test adjustment, or documentation correction. Cleanup happens after the update works, before starting the next update, and again before committing if the diff changed during validation.

### 13.1 Review the completed update

1. Read the complete diff, not only the last edited file.
2. Review adjacent code for duplication, obsolete branches, stale names, and ownership/lifetime mistakes introduced or exposed by the update.
3. Confirm every new helper, declaration, build option, fixture, and file has a current caller or documented boundary purpose.
4. Remove superseded APIs and all temporary logging, tracing, debug output, generated files, and investigation-only code.
5. Confirm the update still respects phase, platform, runtime, and test boundaries.
6. Update user-facing documentation and executable examples from actual source behavior; do not infer syntax or semantics.

### 13.2 Required baseline checks

Run these from the repository root after every minor update:

```bash
git diff --check
python3 scripts/audit_codebase.py
rg -n 'std::system|TODO|FIXME' source tests
rg -n '#if.*(_WIN32|__linux__|__APPLE__)|#ifdef _WIN32' source
```

Search results are reviewed, not blindly rejected: intentional platform conditionals belong in `source/Platform`, and a tracked task marker must identify real remaining work. New shell execution, unexplained markers, or host conditionals in portable code block the update.

Also verify:

- `git status --short` contains no accidental build products or unrelated files;
- new symbols and files have real references;
- removed symbols have no remaining callers, documentation, CMake entries, or fixtures;
- workflow YAML passes `actionlint` when workflows change;
- JavaScript passes `node --check` and website examples pass `svm check` when the website changes.

### 13.3 Code-change validation

For C++ or behavior changes:

```bash
cmake --build build --target simplevm simplevm_tests --parallel 2
./build/bin/simplevm_tests
```

The complete suite must pass with empty unexpected stderr. Changes touching optional LLVM behavior must be validated in both interpreter and LLVM configurations. Changes touching process execution, temporary files, or shared state must also run two test processes concurrently. GCC and Clang warnings-as-errors builds are required for boundary, ownership, portability, and broad refactors.

Safety, ownership, allocator, parser, loader, and broad runtime changes also require the applicable ASan/UBSan/LSan build. Concurrency changes require TSan. Broad boundary refactors require the configured static analyzer. Record which configurations ran; do not replace a required check with an unrelated green build.

Focused and stress tests are not sufficient for a completed feature gate. Run every realistic conformance project activated for that gate from its canonical source under all applicable interpreter/JIT, sanitizer, and platform configurations. Projects live under `tests/projects/`, use deterministic automated inputs/state checks, and may not use future API mocks, compatibility paths, or reduced duplicate sources. Interactive graphics tests separate deterministic simulation checks from manual rendering/input smoke tests; graphics test assets remain test-only dependencies.

Platform-specific changes that cannot be executed locally require the corresponding CI job. Do not claim success for an untested host; inspect the job result and repeat until the relevant required or experimental job passes.

### 13.4 Documentation-only validation

Documentation-only changes still run the baseline cleanup checks. In addition:

- copy complete language examples to temporary `.simple` files and run `svm check`;
- verify commands and paths against the current repository layout;
- verify stated defaults against CMake and install/build scripts;
- verify release/platform claims against current workflows;
- avoid publishing internal CI details in language-facing examples.

A documentation-only update does not require rebuilding unchanged C++, but it must not describe behavior that the source, fixtures, or executable examples contradict.

## 14. Code Review Checklist

Before committing:

- The continuous cleanup gate was run after the final minor update.
- Touched C++ follows the modern C++ ownership and type-safety rules.
- Code follows SRP and does not add unnecessary growth to known monoliths.
- No shim, superseded path, temporary diagnostic, generated artifact, or dead code remains.
- Every new abstraction has current callers and belongs at the correct boundary.
- Public behavior is tested; complete examples pass `svm check`.
- Diagnostics are stable and useful.
- Relevant public documentation matches the implementation.
- Required interpreter, LLVM, warning, static, workflow, and platform checks pass for the change's scope.
- `git diff --check` and `git status --short` are clean apart from the intended files.
