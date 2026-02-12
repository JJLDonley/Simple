# Simple Implementation Plan (API Roadmap)

This document is the authoritative roadmap for shipping and maintaining Simple.

## Supported (Current Baseline)
- Repository layout: `Byte/`, `CLI/`, `Docs/`, `IR/`, `Lang/`, `Tests/`, `VM/`.
- Import resolution supports:
  - reserved stdlib imports
  - relative/absolute imports
  - project-root bare filename lookup (with ambiguity diagnostics)
- Language entry behavior supports script-style programs:
  - top-level statements execute in source order via implicit `__script_entry`
  - top-level function declarations are declarations only (not auto-called)
  - explicit `main` is used as entry only when no top-level script statements exist
- Top-level `return` is rejected at validation time.
- Cast syntax enforced as `@T(value)` (legacy `T(value)` rejected).
- `IO.print`/`IO.println` support typed format placeholders via `{}` in string literals.
- CLI diagnostics are context-rich with source spans and help hints.
- LSP server is implemented and test-covered (lifecycle, diagnostics, navigation, completion, signature help, semantic tokens, rename, code actions).
- CI workflow publishes GitHub Release artifacts for installer consumption.

## Not Supported (v0.1 Non-Goals)
- Package manager ecosystem.
- Full optimizing compiler pipeline.
- AOT-native backend.
- Advanced GC generations/tuning work.

## Planned (Alpha Focus)
- Freeze SBC compatibility/versioning policy in `Docs/Byte.md`.
- Freeze supported SIR subset and unsupported forms list in `Docs/IR.md`.
- Publish explicit supported and deferred language features in `Docs/Lang.md`.
- Freeze CLI command behavior contract and exit-code consistency in `Docs/CLI.md`.
- Define CI matrix and alpha smoke profile that covers the full pipeline.

## Module Plans

### 1. `Simple::Byte` (Format/Loader/Verifier)
Authoritative doc: `Docs/Byte.md`

Status:
- Loader + verifier implemented and heavily tested.

Remaining alpha work:
- Freeze compatibility/versioning policy language in `Docs/Byte.md`.
- Define forward/backward compatibility expectations for SBC artifact consumers.
- Add compatibility smoke test using archived SBC fixtures.

Gate to close:
- Compatibility section in docs is explicit and test-backed.

### 2. `Simple::VM` (Runtime/ABI)
Authoritative doc: `Docs/VM.md`

Status:
- Interpreter baseline stable.
- Core imports + dynamic DL/FFI dispatch implemented.

Remaining alpha work:
- VM behavior, limits, and alpha posture are explicit in docs and reflected in tests.

JIT upgrade posture (interpreter remains canonical):
- Phase 1: scalar parity (no heap/refs, no params).
- Phase 2: locals + params parity (still no heap/refs).
- Phase 3: controlled heap/refs (strings/lists/arrays) with explicit safety checks.
- Phase 4: call support + tier tuning.

### 3. `Simple::IR` (SIR Contract)
Authoritative doc: `Docs/IR.md`

Status:
- SIR parse/validate/lower is stable.

Remaining alpha work:
- Freeze supported SIR subset and unsupported forms list.
- Add regression fixtures for each unsupported-but-diagnosed SIR class.

Gate to close:
- SIR contract is deterministic and release-noted.

### 4. `Simple::Lang` (Front-End)
Authoritative docs: `Docs/Lang.md`, `Docs/StdLib.md`

Status:
- Lexer/parser/validator/emitter implemented.
- Import/extern/global/init flows in place.
- Script-style top-level execution and diagnostics are implemented and test-covered.

Remaining alpha work:
- Publish explicit supported syntax/features list for alpha.
- Publish explicit deferred/unsupported list.
- Confirm diagnostics for parser/lexer/semantic classes meet format contract.

Gate to close:
- Language subset is fully documented and predictable for users.

### 5. `Simple::CLI` (UX + Orchestration)
Authoritative doc: `Docs/CLI.md`

Status:
- `run/build/compile/check/emit/lsp` command surface exists.
- Installer/release scripts integrated with release assets.

Remaining alpha work:
- Freeze command behavior contract.
- Final pass on exit-code/error-format consistency across commands.
- Document installer defaults for `latest` and version-pinned flows.

Gate to close:
- CLI contract is frozen for alpha and release-tested.

### 6. `Simple::LSP` (Editor Protocol + Highlighting)
Authoritative doc: `Docs/LSP.md`

Status:
- `simple lsp` implemented with lifecycle, diagnostics, navigation, completion, signature help, semantic tokens, rename, code actions, and cross-document indexing for open files.
- VS Code extension baseline exists at `Editor/vscode-simple/` with TextMate grammar + language client wiring.

Remaining alpha work:
- Add protocol and editor smoke tests to CI.

Gate to close:
- LSP is functionally usable in editor workflows with diagnostics, navigation, completion, and highlighting.

### 7. `Simple::Tests` (Quality Gates)
Execution references:
- `Tests/tests/*`
- `Docs/Sprint.md`

Status:
- Multi-suite testing exists (`core`, `ir`, `jit`, `lang`, `all`).

Remaining alpha work:
- Define mandatory CI matrix (OS/target/toolchain).
- Add alpha smoke profile that runs quickly but covers end-to-end pipeline.
- Ensure new release pipeline gates on full suite pass.

Gate to close:
- Required CI/test gates are explicit and enforced.

## Release Engineering Plan

### Branching Model
- `main`: active development and integration (release trigger branch).
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
- Finalize docs for Byte/VM/IR/Lang/CLI alpha contract sections.
- Confirm examples run from repository root exactly as documented.

Exit criteria:
- All module docs are current and internally consistent.

### Phase B: LSP + Editor UX
- Add protocol and editor smoke tests to CI.

Exit criteria:
- `simple lsp` is production-usable for core editing workflows.

### Phase C: Runtime + CLI Hardening
- JIT posture decision and documentation.
- CLI error/exit-code consistency pass.
- Smoke scenarios for import resolution and diagnostics.

Exit criteria:
- No known unstable alpha paths are undocumented.

### Phase D: Test and CI Gate Lock
- Finalize CI matrix and enforce full suite gate.
- Define and enforce alpha smoke test profile.
- Add compatibility fixture checks for SBC/SIR subset.

Exit criteria:
- CI is the authoritative merge/release gate.

### Phase E: Release Readiness
- Confirm release workflow output and checksums.
- Dry-run installer (`latest` and pinned version).
- Draft and publish release notes with known limitations.

Exit criteria:
- Clean reproducible release from `main`.

## Alpha Release Checklist
All must be true:
1. `./build.sh --suite all` passes.
2. Release workflow on `main` passes and uploads all artifacts.
3. Installer works for:
   - latest asset path
   - version-pinned asset path
4. Module docs match actual behavior and constraints.
5. Sprint log includes all significant behavior, tooling, and release-contract changes.
6. Release notes explicitly list:
   - compatibility expectations
   - known limitations
   - support posture (especially JIT)

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
