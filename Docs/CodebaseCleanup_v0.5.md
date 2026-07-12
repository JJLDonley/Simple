# Codebase Cleanup Plan before v0.5.1

This plan is the immediate cleanup gate before starting `v0.5.1` native resource foundation work. The goal is to reduce drift, remove legacy naming, split oversized files where it is safe, and make catalog metadata the internal source of truth.

## Goals

- Keep the green baseline intact.
- Remove obvious redundancy and stale tests.
- Replace internal magic strings with enum/typed IDs where practical.
- Replace magic numbers with named constants where they encode runtime rules.
- Move repeated expressions into reusable predicates/helpers.
- Remove redundancy across files before splitting files further.
- Combine duplicate helpers/tables/tests where one shared implementation is enough.
- Keep strings at system boundaries only: parser input, diagnostics, LSP JSON, docs, CLI output, serialized metadata.
- Prepare native resource/process/net/http/terminal work by cleaning registry/catalog boundaries first.

## Non-goals

- No new v0.5.1 features.
- No native resource model rewrite yet.
- No GC replacement.
- No language syntax changes.
- No large risky file split unless behavior is unchanged and tests stay green.

---

# Duplicate/inefficiency discovery strategy

Duplication is not always text-identical. It can be renamed, reordered, hidden in branch chains, or embedded in tests. Use layered discovery before refactoring.

## Mechanical scans

- [ ] Exact duplicate blocks with a clone detector, for example:

  ```bash
  npx jscpd Compiler --pattern "**/*.{cpp,h}" --ignore "**/build/**" --min-lines 8
  ```

- [ ] Repeated string literals:

  ```bash
  rg -No '"[^"]{3,}"' Lang VM Byte Library LSP CLI Tests \
    | sort | uniq -c | sort -nr | head -100
  ```

- [ ] Repeated numeric literals:

  ```bash
  rg -n '\b[0-9]{2,}\b|0x[0-9A-Fa-f]{2,}' Lang VM Byte Library LSP CLI
  ```

Classify numeric literals as either harmless fixture/example values or real rules that need named constants.

## Structural/static-analysis scans

Use `clang-tidy` and similar tools for renamed/structural duplication:

- `bugprone-branch-clone`
- `bugprone-copy-paste`
- `misc-redundant-expression`
- `readability-magic-numbers`
- `readability-function-size`
- `performance-unnecessary-copy-initialization`
- `performance-for-range-copy`
- `modernize-*` where safe

Custom Semgrep/Coccinelle-style patterns should look for:

- repeated `if (error) *error = "..."` diagnostics;
- repeated `module == "System.X"` or `member == "name"` checks;
- repeated `args.size() == N` validation;
- repeated native registration shapes;
- repeated resource handle validation branches.

## Same-concept/different-name audits

Generate targeted reports and group by concept:

```bash
rg -n 'bool Is|bool Check|std::string.*Name|ToString|FromString|Parse|Canonical|Display' \
  Lang VM Byte Library LSP CLI
```

Look for duplicate concepts under different names:

- type predicates;
- library module/member parsing;
- enum/string conversions;
- diagnostic builders;
- native signature construction;
- resource/handle validation;
- LSP JSON helpers;
- CLI/test command runners;
- temp-file/path helpers;
- bytecode fixture builders.

## Table-shaped code hidden as branches

Search for large switch/if chains:

```bash
rg -n 'switch \(|else if|if \(.*==' Lang VM Byte Library LSP CLI
```

If a branch only maps ID/name to data, replace it with a `constexpr std::array` or catalog table. Good candidates:

- opcode metadata;
- library modules/members;
- native function specs;
- capability names;
- platform availability;
- type classifications;
- diagnostic categories.

## Inefficiency scans

Look for avoidable copies or repeated work:

```bash
rg -n 'std::string .*=' Lang VM Byte Library LSP CLI
rg -n 'std::vector<.*> .*=' Lang VM Byte Library LSP CLI
rg -n 'substr\(|find\(|rfind\(|\+ ' Lang VM Byte Library LSP CLI
```

Prefer:

- `std::string_view` for non-owning strings;
- `std::span` for non-owning arrays;
- `const&` for large structs;
- typed IDs over repeated string parsing;
- `constexpr std::array` for static tables;
- lookup maps built once rather than on every call.

Use profiler data before doing performance-motivated rewrites:

```bash
perf record ./build/bin/svm check Tests/simple/hello.simple
perf report
```

## Audit script target

Create `scripts/audit_codebase.py` to report:

- [x] largest files;
- [ ] largest functions;
- [x] duplicate test names;
- [x] unreferenced fixtures;
- [x] public alias imports outside migration tests;
- [x] top repeated string literals;
- [x] top repeated numeric literals;
- [x] stale diagnostic substrings;
- [ ] direct `"System."` / `"Standard."` string checks outside allowed boundary files;
- [ ] repeated native registration patterns where detectable.

## Refactor rule

For every duplicate/inefficient pattern:

1. Identify and classify the pattern.
2. Create one shared helper/table only if at least two call sites use it immediately.
3. Replace a small number of call sites first.
4. Run the full relevant tests.
5. Continue replacement in small slices.
6. Add a guard test/script if the duplication should not return.

---

# Cleanup slices

## `cleanup.1` — Test hygiene and drift guards

Status: partially started.

Deliverables:

- [x] Remove duplicate registered test entries found during audit.
- [x] Remove stale duplicate fixture `Tests/simple_bad/module_var_access.simple`.
- [x] Register previously unreferenced positive fixtures.
- [x] Add a meta-test that fails on duplicate registered test names within a section.
- [x] Add a meta-test that fails when `Tests/simple/*.simple` is not referenced or explicitly ignored.
- [x] Add a meta-test that fails when `Tests/simple_bad/*.simple` is not referenced or explicitly ignored.
- [x] Add a fixture scan that rejects public alias imports except explicit migration/diagnostic tests:
  - `import IO`
  - `import FS`
  - `import DL`
  - `import Time`
  - `import Buffer`
  - `import Channel`
- [ ] Add a WIP/API policy test:
  - implemented catalog members must have signatures;
  - native-backed implemented members must have native metadata;
  - planned/WIP members must not accidentally dispatch;
  - unavailable members must produce clear diagnostics.

Exit criteria:

- Full suite remains green.
- Fixture/test registration drift is caught automatically.

## `cleanup.2` — Legacy diagnostic names

Problem:

Some internal diagnostics/tests still use old public-ish names even though the final public model is canonical `System.*` / `Standard.*` only.

Known stale diagnostic names:

- `IO.print` should become `Standard.IO.print`.
- `DL.open` should become `System.FFI.open`.
- `File.open`, `File.close`, `File.write` should become `System.FS.open`, `System.FS.close`, `System.FS.write`.
- `IO.buffer_*` should become `System.Buffer.*` or `System.Bytes.*`, depending on actual catalog ownership.

Deliverables:

- [ ] Add a reusable diagnostic-name helper:
  - input: module/member ID or canonical strings;
  - output: canonical display name.
- [x] Update TAST call diagnostics to canonical names.
- [x] Update SIR lowering diagnostics to canonical names.
- [x] Update language validation diagnostics to canonical names.
- [x] Update tests to expect canonical diagnostics.
- [ ] Add a test rejecting stale diagnostic substrings where appropriate.

Exit criteria:

- Runtime/compiler diagnostics no longer suggest legacy public APIs.

## `cleanup.3` — Catalog-source-of-truth tightening

Problem:

Library facts still leak into several places: catalog, TAST calls, validation, SIR lowering, native registry, LSP, docs, and tests.

Deliverables:

- [ ] Define/standardize typed IDs for catalog lookup:
  - `LibraryRoot`
  - `LibraryModuleId`
  - per-module member enums or a stable unified member ID
- [ ] Add helpers for canonical display/input conversion:
  - `ToString(SystemModule)`
  - `ToString(StandardModule)`
  - `ParseLibraryModuleName(std::string_view)`
  - `CanonicalLibraryModuleName(LibraryModuleId)`
  - `CanonicalLibraryMemberName(...)`
- [ ] Replace ad hoc internal string comparisons with typed helpers in low-risk files first.
- [ ] Keep string comparisons only at parser/LSP/docs/CLI/serialization boundaries.
- [ ] Add tests that catalog module/member string round-trips are complete.

Exit criteria:

- Library module/member identity is mostly enum/typed-ID based in compiler/runtime internals.

## `cleanup.4` — Native registry pre-split

Problem:

`VM/src/native/registry.cpp` is already large and will grow significantly in v0.5.1-v0.6.

Deliverables:

- [ ] Split registration specs by domain without changing behavior:
  - `VM/src/native/specs/io_specs.cpp`
  - `VM/src/native/specs/fs_specs.cpp`
  - `VM/src/native/specs/path_specs.cpp`
  - `VM/src/native/specs/env_specs.cpp`
  - `VM/src/native/specs/ffi_specs.cpp`
  - `VM/src/native/specs/buffer_specs.cpp`
  - `VM/src/native/specs/bytes_specs.cpp`
  - `VM/src/native/specs/channel_specs.cpp`
  - `VM/src/native/specs/thread_specs.cpp`
- [ ] Keep `registry.cpp` as orchestration only.
- [ ] Introduce `NativeFunctionSpec` builders/helpers if current registration uses repeated positional arguments.
- [ ] Replace boolean clusters with named enum fields:
  - blocking behavior;
  - allocation behavior;
  - safepoint behavior;
  - platform availability;
  - capability requirements.

Exit criteria:

- Adding `Process`, `Net`, `HTTP`, `Terminal`, and `Promise` does not bloat one registry file.

## `cleanup.5` — Reusable predicates and named constants

Problem:

Several repeated expressions and numeric constants encode rules without names.

Deliverables:

- [ ] Add common type predicates where repeated:
  - `IsScalarType`
  - `IsNumericType`
  - `IsIntegerType`
  - `IsSignedIntegerType`
  - `IsFloatType`
  - `IsReferenceType`
  - `IsHandleType`
  - `IsPromiseType`
  - `IsChannelType`
- [ ] Add library predicates:
  - `IsCanonicalLibraryModuleName`
  - `IsSystemModule`
  - `IsStandardModule`
  - `IsImplementedLibraryMember`
  - `IsPlannedLibraryMember`
- [ ] Replace magic numbers with named constants for runtime/ABI concepts:
  - handle packing/generation shifts/masks;
  - bytecode header/section sizes;
  - default stack/heap/resource limits;
  - native buffer defaults;
  - timeout/poll constants;
  - test ports and temporary path prefixes.
- [ ] Do not rename numeric values used only as obvious fixture arithmetic unless they encode a rule.

Exit criteria:

- Repeated rule expressions become named reusable helpers.

## `cleanup.6` — Redundancy removal and shared utilities

Problem:

Code splitting should not just move large blocks into smaller files. It should first identify repeated logic and collapse it into shared domain utilities. Otherwise the codebase becomes smaller files with the same duplication.

Targets for consolidation:

- repeated library module/member string parsing and formatting;
- repeated native signature construction;
- repeated diagnostic string construction;
- repeated type predicate expressions;
- repeated VM bytecode builder/test fixture helpers;
- repeated LSP JSON message/response builders;
- repeated CLI command/run/capture helpers;
- repeated docs/table formatting helpers;
- repeated temporary-file/path helpers in tests;
- repeated resource/handle validation branches;
- repeated import/native dispatch lookup code.

Deliverables:

- [ ] Audit duplicate helpers/functions across `Lang`, `VM`, `Byte`, `LSP`, `CLI`, and `Tests`.
- [ ] Create shared domain utility files only where reuse is real, not speculative.
- [ ] Prefer one table-driven implementation over multiple switch/string chains.
- [x] Merge equivalent CLI test command/temp/capture helpers and remove duplicate local copies.
- [x] Merge repeated test-section registration logic into one shared append helper.
- [ ] Replace copied diagnostics with reusable diagnostic builders.
- [ ] Replace copied native metadata construction with `NativeFunctionSpec` helpers/builders.
  - [x] Add typed `LibraryModuleId` native spec builder and migrate Random/OS/Thread registrations.
- [x] Replace copied LSP signature-help response snippets with small JSON builder helpers.
- [ ] Replace copied command execution helpers in CLI tests with one shared test utility.
- [ ] Add comments only where the shared abstraction encodes a non-obvious rule.

Exit criteria:

- File splits reduce total duplicated logic, not just per-file line counts.
- New shared helpers are used by at least two call sites immediately.
- No broad abstraction is added without current concrete reuse.

## `cleanup.7` — Oversized file seams

Problem:

Several files are too large. Full splits can be risky, so first create seams and move low-risk isolated logic. Splitting should happen after redundancy removal so shared logic is extracted once.

Targets:

- `Lang/src/lang_sir.cpp`
- `Lang/src/lang_validate.cpp`
- `LSP/src/lsp_server.cpp`
- `VM/src/jit/llvm_backend.cpp`
- `VM/src/vm.cpp`
- `Byte/src/sbc_verifier.cpp`
- `Tests/tests/test_core.cpp`
- `Tests/tests/test_ir.cpp`
- `Tests/tests/test_jit.cpp`

Deliverables:

- [ ] Move pure helpers into domain helper files first.
- [ ] Split tests by domain before splitting runtime behavior.
- [ ] Avoid behavior changes in file-split commits.
- [ ] Require full suite green after each split.

Suggested first seams:

- `LSP/src/library_lsp.cpp` for library completions/hover/signature helpers.
- `Lang/src/SIR/library_lowering.cpp` for library/reserved call lowering.
- `Lang/src/Validate/library_validate.cpp` for library/reserved call validation.
- `Byte/src/verifier/resource_verifier.cpp` once resource-kind verifier work begins.
- `Tests/tests/vm/test_imports.cpp` or `Tests/tests/vm/test_resources.cpp` for import/resource tests.

Exit criteria:

- v0.5.1 can add new native-resource logic without touching giant mixed-purpose files unnecessarily.

---

# Recommended order

1. `cleanup.1` test hygiene and drift guards.
2. `cleanup.2` canonical diagnostic names.
3. `cleanup.3` catalog-source-of-truth helpers.
4. `cleanup.5` reusable predicates and named constants.
5. `cleanup.4` native registry pre-split.
6. `cleanup.6` redundancy removal and shared utilities.
7. `cleanup.7` oversized file seams as needed.

This order front-loads safety and drift prevention before larger refactors. It also ensures file splitting removes duplication instead of preserving duplicated logic in more places.

---

# Acceptance gate before v0.5.1

Before starting `v0.5.1`, complete at minimum:

- [ ] duplicate-test-name guard;
- [ ] unreferenced-fixture guard;
- [ ] no-public-alias fixture guard;
- [ ] canonical diagnostic-name cleanup for obvious legacy names;
- [ ] typed catalog display/parse helpers;
- [ ] named enums for native blocking/allocation/safepoint behavior if not already complete;
- [ ] full suite green;
- [ ] JIT suite green;
- [ ] docs/timeline updated if scope changes.

The native registry split can happen before or during early `v0.5.1`, but `Process`/`Net`/`HTTP`/`Terminal` should not be added until registry/spec organization is ready.
