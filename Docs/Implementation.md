# Simple Implementation Plan (Alpha -> Release)

This is the active execution plan for shipping and maintaining Simple from repository root layout.

## Scope

### Primary Goal
- Ship a stable, documented pipeline:
  - `.simple` -> `SIR` -> `SBC` -> verified runtime execution.

### Alpha Deliverable
- Consistent interpreter-first runtime behavior.
- Stable CLI UX for `simple` and `simplevm`.
- Release automation that produces installable artifacts.
- Docs and tests that match real behavior.

### Non-Goals (v0.1)
- Package manager ecosystem.
- Full optimizing compiler pipeline.
- AOT-native backend.
- Advanced GC generations/tuning work.

## Current Baseline

- Root layout migration is complete (`Byte/`, `CLI/`, `Docs/`, `IR/`, `Lang/`, `Tests/`, `VM/`).
- Import resolution supports:
  - reserved stdlib imports,
  - relative/absolute imports,
  - project-root bare filename lookup (with ambiguity diagnostics).
- Language entry behavior supports script-style programs:
  - top-level statements are emitted/executed in source order via implicit `__script_entry`,
  - top-level function declarations are declarations only (not auto-called),
  - explicit `main` is used as entry when no top-level script statements exist.
- Top-level `return` is explicitly rejected at validation time.
- Cast syntax is enforced as `@T(value)` (legacy bare `T(value)` cast style is rejected).
- `IO.print`/`IO.println` support typed format placeholders via string-literal `{}` forms.
- CLI diagnostics are context-rich and include source spans + help hints.
- LSP server is implemented and test-covered (lifecycle, diagnostics, navigation, completion, signature help, semantic tokens, rename/code-actions).
- Main branch CI workflow publishes GitHub Release artifacts for installer consumption.

## Module Plan

### 1. `Simple::Byte` (Format/Loader/Verifier)

Authoritative doc:
- `Docs/Byte.md`

Status:
- Loader + verifier are implemented and heavily tested.

Remaining alpha work:
- [ ] Freeze compatibility/versioning policy language in `Docs/Byte.md`.
- [ ] Define forward/backward compatibility expectations for SBC artifact consumers.
- [ ] Add one explicit compatibility smoke test using archived SBC fixture(s).

Gate to close:
- Compatibility section in docs is explicit and test-backed.

### 2. `Simple::VM` (Runtime/ABI)

Authoritative doc:
- `Docs/VM.md`

Status:
- Interpreter baseline stable.
- Core imports + dynamic DL/FFI dispatch implemented.

Remaining alpha work:
- [x] Finalize JIT posture for alpha: experimental, enabled-by-default in `ExecuteModule` (no CLI flag yet).
- [x] Remove/label placeholder runtime paths that are not alpha-grade (see `Docs/VM.md`).
- [x] Add focused runtime smoke profile for import + trap diagnostics (`simplevm_tests --smoke`).
- [x] JIT upgrade plan (scope + milestones) is documented and tracked.

JIT upgrade plan (interpreter must remain canonical):
- [x] Phase 1: scalar parity (no heap/refs, no params).
- [x] Phase 1 scope: scalar consts, unary ops, arithmetic, compare, bool, bitwise, shifts, and numeric conversions for i32/i64/u32/u64/f32/f64.
- [x] Phase 1 guardrails: zero-parameter functions only, no heap/refs, no globals, no calls.
- [x] Phase 1 tests: assert compiled exec counts for new opcode coverage.
- [x] Phase 2: locals + params parity (still no heap/refs).
- [x] Phase 2 scope: allow parameters and local access for scalar-only functions.
- [x] Phase 2 guardrails: enforce type-safe stack discipline and retain fallback-on-failure.
- [x] Phase 3: controlled heap/refs (strings/lists/arrays).
- [x] Phase 3 scope: opt-in compiled support for ref-like ops with explicit safety checks.
- [x] Phase 3 guardrails: gate by verifier metadata (stack maps) where needed for GC safety.
- [ ] Phase 4: call support + tier tuning.
- [x] Phase 4 scope: allow compiled intra-module calls when callee is compiled-safe.
- [x] Phase 4 tuning: revisit tier thresholds and heuristics with bench coverage (`scripts/jit_bench.sh --iters 200`, 2026-02-12).
- [x] Phase 4 tuning: add env overrides for tier thresholds (SIMPLE_JIT_TIER0/TIER1/OPCODE).
- Phase 2: locals + params parity (still no heap/refs)
  - Allow parameters and local access for scalar-only functions.
  - Enforce type-safe stack discipline and retain fallback-on-failure.
- Phase 3: controlled heap/refs (strings/lists/arrays)
  - Add opt-in compiled support for ref-like ops with explicit safety checks.
  - Gate by verifier metadata (stack maps) where needed for GC safety.
- Phase 4: call support + tier tuning
  - Allow compiled intra-module calls when callee is compiled-safe.
  - Revisit tier thresholds and heuristics with bench coverage.

Gate to close:
- VM behavior, limits, and alpha posture are explicit in docs and reflected in tests.

### 3. `Simple::IR` (SIR Contract)

Authoritative doc:
- `Docs/IR.md`

Status:
- SIR parse/validate/lower is stable.

Remaining alpha work:
- [ ] Freeze supported SIR subset and unsupported forms list.
- [ ] Add regression fixtures for each unsupported-but-diagnosed SIR class.

Gate to close:
- SIR contract is deterministic and release-noted.

### 4. `Simple::Lang` (Front-End)

Authoritative docs:
- `Docs/Lang.md`
- `Docs/StdLib.md`

Status:
- Lexer/parser/validator/emitter implemented.
- Import/extern/global/init flows in place.
- Script-style top-level execution and diagnostics are implemented and test-covered.

Remaining alpha work:
- [ ] Publish explicit supported syntax/features list for alpha.
- [ ] Publish explicit deferred/unsupported list.
- [ ] Confirm diagnostics for parser/lexer/semantic classes meet format contract.

Gate to close:
- Language subset is fully documented and predictable for users.

### 5. `Simple::CLI` (UX + Orchestration)

Authoritative doc:
- `Docs/CLI.md`

Status:
- `run/build/compile/check/emit/lsp` command surface exists.
- Installer/release scripts integrated with release assets.

Remaining alpha work:
- [ ] Freeze command behavior contract.
- [ ] Final pass on exit-code/error-format consistency across commands.
- [ ] Document installer defaults for `latest` and version-pinned flows.

Gate to close:
- CLI contract is frozen for alpha and release-tested.

### 6. `Simple::LSP` (Editor Protocol + Highlighting)

Authoritative doc:
- `Docs/LSP.md`

Status:
- `simple lsp` is implemented with protocol lifecycle, diagnostics, navigation features (hover/definition/declaration/documentHighlight/references), completion, rename, code actions, workspace symbols, signature help, semantic tokens, and cross-document indexing for open files.
- completion includes import-path mode inside import declarations (quoted and unquoted), covering `System.*` aliases, legacy reserved modules, and open-document module stems.
- completion now also surfaces reserved-module member suggestions for active import aliases (for example `DL.call_i32` from `import "Core.DL" as DL`).
- code-action quick-fix declaration insertion respects top-of-file import headers.
- signature help now resolves reserved-module alias members with parameter labels (for example `OS.args_get(index)`).
- signature help now preserves IO format/value overload semantics for IO aliases imported via `as`.
- signature help now emits overload-aware signatures for reserved aliases where language semantics support multiple call forms (for example `DL.open(path)` and `DL.open(path, manifest)`).
- hover now renders reserved-module alias callable signatures (for example `OS.args_get(index) -> string`) when no local declaration type exists.
- undeclared-identifier quick-fix code action infers numeric declaration type from assignment usage (`i32` vs `f64`) before emitting the declaration edit.
- rename/prepareRename now protect reserved imported module member symbols from edit operations to avoid invalid API symbol rewrites.
- documentHighlight now differentiates write contexts (declarations and assignment targets) from read contexts for local symbol occurrences.
- undeclared-identifier quick-fix inference now covers scalar literals beyond numeric (`bool`/`string`/`char` plus numeric `i32`/`f64`).
- hover signature enrichment now includes IO aliases (for example `Out.println(value) -> void`) in addition to reserved Core aliases.
- VS Code extension baseline exists at `Editor/vscode-simple/` with TextMate grammar + language client wiring.
- signature-help coverage includes IO format-call variants and `@T(value)` cast-call help.

Next-module execution focus:
- [x] Implement stdio JSON-RPC LSP server lifecycle (`initialize`, `shutdown`, `exit`).
- [x] Implement document sync and publish diagnostics from `Simple::Lang` parser/validator.
- [x] Implement navigation/features:
  - hover, definition, references, document symbols, completion.
- [x] Implement syntax highlighting:
  - semantic tokens (`textDocument/semanticTokens/full`),
  - TextMate grammar fallback for editor startup/non-LSP highlight.
- [x] Add integration tests for protocol lifecycle, diagnostics, and semantic token snapshots.
- [x] Add release automation for VS Code extension artifacts to publish `.vsix` from CI/release tags.
- [x] Build/ship VS Code extension install docs and quickstart validation checklist.

Gate to close:
- LSP is functionally usable in editor workflows with diagnostics, navigation, completion, and highlighting.

### 7. `Simple::Tests` (Quality Gates)

Execution references:
- `Tests/tests/*`
- `Docs/Sprint.md`

Status:
- Multi-suite testing exists (`core`, `ir`, `jit`, `lang`, `all`).

Remaining alpha work:
- [ ] Define mandatory CI matrix (OS/target/toolchain).
- [ ] Add alpha smoke profile that runs quickly but covers end-to-end pipeline.
- [ ] Ensure new release pipeline gates on full suite pass.

Gate to close:
- Required CI/test gates are explicit and enforced.

## Release Engineering Plan

### Branching Model
- `main`: active development and integration.
- `main` also acts as the release trigger branch.
- `gh-pages`: static project website.

### Artifact Contract
For Linux x86_64 release jobs, publish:
- `simple-<version>-linux-x86_64.tar.gz`
- `simple-<version>-linux-x86_64.tar.gz.sha256`
- `simple-latest-linux-x86_64.tar.gz`
- `simple-latest-linux-x86_64.tar.gz.sha256`

### Installer Contract
`install.sh` supports:
- local archive: `--from-file`
- explicit URL: `--url`
- GitHub auto-resolution by repo/version:
  - latest
  - version-pinned tag

## Phase Plan (Execution Order)

### Phase A: Contract Freeze
- [ ] Finalize docs for Byte/VM/IR/Lang/CLI alpha contract sections.
- [x] Remove stale path references (`Simple/...`) from active module docs.
- [ ] Confirm examples run from repository root exactly as documented.

Exit criteria:
- All module docs are current and internally consistent.

### Phase B: LSP + Editor UX
- [x] Ship M1 server skeleton from `Docs/LSP.md` (lifecycle + open/change/close + diagnostics).
- [x] Ship M2 navigation/completion features (hover/definition/references/document symbols/completion).
- [x] Ship M3 highlighting stack (semantic tokens + TextMate grammar fallback).
- [ ] Add protocol and editor smoke tests to CI.

Exit criteria:
- `simple lsp` is production-usable for core editing workflows.

### Phase C: Runtime + CLI Hardening
- [ ] JIT posture decision and documentation.
- [ ] Phase 1 JIT scalar parity (see JIT upgrade plan).
- [ ] CLI error/exit-code consistency pass.
- [ ] Smoke scenarios for import resolution and diagnostics.

Exit criteria:
- No known unstable alpha paths are undocumented.

### Phase D: Test and CI Gate Lock
- [ ] Finalize CI matrix and enforce full suite gate.
- [ ] Define and enforce alpha smoke test profile.
- [ ] Add compatibility fixture checks for SBC/SIR subset.

Exit criteria:
- CI is the authoritative merge/release gate.

### Phase E: Release Readiness
- [ ] Confirm release workflow output and checksums.
- [ ] Dry-run installer (`latest` and pinned version).
- [ ] Draft and publish release notes with known limitations.

Exit criteria:
- Clean reproducible release from `main`.

## Alpha Release Checklist

All must be true:

1. `./build.sh --suite all` passes.
2. Release workflow on `main` passes and uploads all artifacts.
3. Installer works for:
   - latest asset path,
   - version-pinned asset path.
4. Module docs match actual behavior and constraints.
5. Sprint log includes all significant behavior, tooling, and release-contract changes.
6. Release notes explicitly list:
   - compatibility expectations,
   - known limitations,
   - support posture (especially JIT).

## Verification Commands

### Local Quality
- `./build.sh --suite core`
- `./build.sh --suite ir`
- `./build.sh --suite jit`
- `./build.sh --suite lang`
- `./build.sh --suite all`

### Local Release Dry Run
- `./release.sh --version vX.Y.Z --target linux-x86_64`
- `./install.sh --from-file ./dist/simple-vX.Y.Z-linux-x86_64.tar.gz --version vX.Y.Z`

### Installer (GitHub)
- `./install.sh --repo <owner/repo> --version latest`
- `./install.sh --repo <owner/repo> --version vX.Y.Z`
