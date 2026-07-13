# v0.6 Library Domain Audit

Purpose: define what must be finished before `v0.6` is considered stable for the canonical `System.*` / `Standard.*` library architecture.

Current baseline at audit start:

- Tool version: `v0.5.0`
- Full test suite: `1809/1809`
- JIT section: `61/61`
- Public import model: canonical `System.X` / `Standard.X` only
- Internal catalog: enum-backed module/member catalog with centralized signatures and implementation metadata

## v0.6 stability rule

`v0.6` is stable when every non-deferred library domain is:

1. catalog-complete: module, member enum, member names, signatures, metadata, and docs are centralized;
2. implemented or explicitly unavailable with a deliberate diagnostic;
3. validated by tests for successful behavior and expected failure behavior;
4. represented consistently in compiler, runtime/native registry, LSP, docs, and website;
5. free of public aliases and legacy names except explicit migration diagnostics.

## Explicit WIP/deferred domains

These domains remain WIP for now and do **not** block `v0.6` if they are cleanly reserved/unavailable and documented:

| Domain | v0.6 requirement |
|---|---|
| `System.Net` | reserved/cataloged; no accidental callable API |
| `System.HTTP` | reserved/cataloged; no accidental callable API |
| `System.Process` | reserved/cataloged; no accidental callable API |
| `System.ASM` | reserved/cataloged; no accidental callable API |
| `Standard.HTTP` | reserved/cataloged; no accidental callable API |
| `Standard.Promise` | reserved/cataloged; no accidental callable API |

Important dependency note: `Standard.Process`, `Standard.Net`, and `Standard.HTTPS` are not in the explicit WIP list, but their natural backing depends on deferred `System.Process`, `System.Net`, and `System.HTTP`. For `v0.6`, either:

- move those Standard domains into explicit WIP too, or
- implement their required System backing, or
- make them unavailable/reserved with diagnostics and document that exception.

## Domain status summary

Legend:

- **Green target**: must be fully working for `v0.6`.
- **Partial**: exists and has tests, but missing members/signatures/backing/docs.
- **Reserved**: cataloged but intentionally unavailable.
- **Deferred WIP**: explicitly excluded from `v0.6` completion.
- **Decision required**: cannot be honestly green without clarifying scope/backing.

### System domains

| Domain | Current audit status | v0.6 target | Main gaps |
|---|---:|---:|---|
| `System.IO` | Partial | Green target | Real low-level handle API (`stdin/stdout/stderr/write/writeText/flush`) vs current buffer compatibility helpers; tests/docs/signatures. |
| `System.FS` | Partial | Green target | Finish low-level file/dir handle operations (`flush/seek/tell/stat/nextDirEntry/closeDir/rename`), capability metadata, Result/Option plan or transitional shape docs. |
| `System.Path` | Partial | Green target | Finish `absolute`, `relative`; remove stale `exists/isFile/isDir` from System.Path surface or document rejection; tests. |
| `System.Env` | Partial/mostly working | Green target | Confirm `argsCount/arg/get/set/unset/exePath` semantics on all platforms; add negative tests and docs. |
| `System.OS` | Partial/mostly working | Green target | Confirm `platform/arch/isLinux/isMacos/isWindows/pid/cpuCount/pageSize/exit/sleepMs`; remove stale env/platform overlap; tests/docs. |
| `System.Time` | Partial | Green target | Implement/document `sleepNs`, `sleepMs`, timer handles or explicitly reserve them; remove snake-case compatibility from public docs if retained only as legacy. |
| `System.FFI` | Partial/working core | Green target | Canonicalize docs/tests on `open/symbol/sym/close/lastError/supported`; decide status of scalar `call*` helpers; capability and cleanup metadata. |
| `System.Buffer` | Partial | Green target | Finish `get/set/readU64LE/writeU64LE`; clarify mutable buffer vs current `i32[]` backing; tests for bounds/endian behavior. |
| `System.Bytes` | Partial | Green target | Decide immutable byte value representation vs current `i32[]`; finish `get/set/readU64LE/writeU64LE` or reserve incompatible mutating members; conversion tests. |
| `System.Json` | Partial | Green target | Finish `kind/get/at/len/asString/asI64/asF64/asBool`; handle lifecycle/error behavior; tests for arrays/objects/types/free. |
| `System.Log` | Partial | Green target | Confirm low-level-only `log/setLevel/setFile/flush`; move/keep convenience `info/warn/error` only under Standard or reserve; tests/docs. |
| `System.Random` | Partial | Green target | Confirm `seed/i32/i64/f64/fillBytes`; move `range` to Standard only or explicitly reserve; deterministic seed tests. |
| `System.Thread` | Partial | Green target | Confirm `yield/sleep/sleepMs/hardwareConcurrency`; decide `spawn/join/detach` status; avoid unsafe closure/rooting until ready. |
| `System.Channel` | Partial/working typed families | Green target | Verify all typed families (`I32/I64/F32/F64/Bool/String/Bytes`) have signatures, native backing, using/import tests, close semantics, pending semantics. |
| `System.Job` | Reserved | Decision required | Not listed as WIP, but depends on Promise/job runtime. Either defer explicitly or implement/reserve with diagnostics. |
| `System.Process` | Reserved | Deferred WIP | Keep unavailable; no accidental Standard dependency. |
| `System.Net` | Reserved | Deferred WIP | Keep unavailable; no accidental Standard dependency. |
| `System.HTTP` | Reserved | Deferred WIP | Keep unavailable; no accidental Standard dependency. |
| `System.ASM` | Reserved | Deferred WIP | Keep unavailable; no accidental callable API. |
| `System.Terminal` | Reserved | Decision required | Not listed as WIP. Either implement terminal primitives or defer explicitly. |
| `System.Capability` | Reserved | Decision required | Capability policy is needed for mature native APIs; either implement `has/require/deny` or defer explicitly. |
| `System.Runtime` | Reserved | Decision required | Could be small enough for `version/jitEnabled/jitStats`; decide and test. |
| `System.Debug` | Reserved | Decision required | Could remain debug-gated; decide whether WIP or required. |

### Standard domains

| Domain | Current audit status | v0.6 target | Main gaps |
|---|---:|---:|---|
| `Standard.IO` | Partial/working print | Green target | Finish/define `readLine`; confirm format variants, scalar validation, and no System.IO handle leakage. |
| `Standard.Console` | Reserved | Decision required | Not WIP; either implement basic console wrappers or defer explicitly. |
| `Standard.FS` | Partial | Green target | Finish high-level `appendText/move/ensureDir/list/walk` or reserve deliberately; align with System.FS completed surface. |
| `Standard.Path` | Partial | Green target | Finish `absolute/relative`; tests for platform separators and normalization. |
| `Standard.Buffer` | Reserved | Green target or defer | User wants libraries flushed out; implement high-level growable/cursor helpers or explicitly defer. |
| `Standard.Bytes` | Partial | Green target | Finish `fromString/toString/concat/toHex/fromHex/toBase64/fromBase64`; clarify byte value representation. |
| `Standard.Text` | Reserved | Green target or defer | Define and implement core string helpers (`len/isEmpty/contains/startsWith/endsWith/trim/split/join/replace`) or defer explicitly. |
| `Standard.Json` | Reserved | Green target or defer | Either implement high-level JsonValue wrapper over System.Json or keep unavailable with explicit docs. |
| `Standard.Math` | Partial | Green target | Finish `clamp/lerp`; confirm `PI/abs/min/max/sqrt` generic behavior and diagnostics. |
| `Standard.Random` | Partial | Green target | Finish `bool/bytes/fillBytes`; deterministic tests. |
| `Standard.Time` | Partial | Green target | Confirm `monoNs/nowNs/sleepMs/formatWallNs`; decide duration/instant helpers. |
| `Standard.Log` | Partial | Green target | Finish `debug` or reserve; confirm `info/warn/error/setLevel/setFile` docs/tests. |
| `Standard.Process` | Reserved | Decision required | Depends on deferred `System.Process`; either move to WIP or keep unavailable with docs. |
| `Standard.Net` | Reserved | Decision required | Depends on deferred `System.Net`; either move to WIP or keep unavailable with docs. |
| `Standard.HTTP` | Reserved | Deferred WIP | Keep unavailable; no accidental callable API. |
| `Standard.HTTPS` | Reserved | Decision required | Depends on deferred System HTTP/Net; likely WIP unless separately scoped. |
| `Standard.Terminal` | Reserved | Decision required | Depends on `System.Terminal`; either implement both or defer both. |
| `Standard.Promise` | Reserved | Deferred WIP | Keep unavailable; no accidental callable API. |
| `Standard.Channel` | Reserved | Green target or defer | Generic wrapper needs runtime/generics semantics; either implement minimal typed wrapper or defer explicitly. |
| `Standard.Collections` | Reserved | Green target or defer | Source-level generic helpers need language/runtime support; decide scope. |
| `Standard.Result` | Reserved | Green target or defer | If Result is not a language value yet, keep reserved/unavailable; otherwise implement helpers. |
| `Standard.Option` | Reserved | Green target or defer | If Option is not a language value yet, keep reserved/unavailable; otherwise implement helpers. |

## v0.6 recommended green-list

To keep `v0.6` achievable while honoring the explicit WIP list, target these as fully green:

### System green-list

- `System.IO`
- `System.FS`
- `System.Path`
- `System.Env`
- `System.OS`
- `System.Time`
- `System.FFI`
- `System.Buffer`
- `System.Bytes`
- `System.Json`
- `System.Log`
- `System.Random`
- `System.Thread`
- `System.Channel`

### Standard green-list

- `Standard.IO`
- `Standard.FS`
- `Standard.Path`
- `Standard.Buffer` or explicitly defer
- `Standard.Bytes`
- `Standard.Text` or explicitly defer
- `Standard.Json` or explicitly defer
- `Standard.Math`
- `Standard.Random`
- `Standard.Time`
- `Standard.Log`

## Scope decisions needed before implementation

These decisions should be made before coding v0.6, because they affect what “fully finished” means:

1. Are `System.Job`, `System.Terminal`, `System.Capability`, `System.Runtime`, and `System.Debug` required for v0.6, or should they join WIP?
2. Are `Standard.Process`, `Standard.Net`, `Standard.HTTPS`, `Standard.Terminal`, `Standard.Channel`, `Standard.Collections`, `Standard.Result`, and `Standard.Option` required for v0.6, or should they join WIP?
3. Should `System.Buffer` / `System.Bytes` remain backed by `i32[]` for v0.6, or should v0.6 introduce a real byte/handle representation?
4. Should `Result<T>` / `Option<T>` be real v0.6 language/runtime values, or should native APIs keep transitional return shapes (`bool`, handles, strings) until later?
5. Should legacy snake-case members (`mono_ns`, `wall_ns`, `last_error`) remain callable as catalog members, or become legacy diagnostics only?

## Required audit tasks

- [ ] Generate a machine-readable catalog report: module, member, signature, implemented/planned, backing, docs, tests.
- [ ] Fail tests if a member is marked implemented but lacks a signature.
- [ ] Fail tests if a member has native backing but no `LibraryModuleId` metadata.
- [ ] Fail tests if docs mention short imports outside migration diagnostics.
- [ ] Add fixture per green-list module covering import, using, direct member access, success path, and at least one failure/diagnostic path.
- [ ] Add LSP coverage for completion/hover/signature/semantic tokens for every green-list module.
- [ ] Add native registry validation for all implemented System members.
- [ ] Add generated docs from catalog metadata so docs cannot drift from implementation.

## Green-light checklist for v0.6

`v0.6` can be cut when:

- [ ] all explicit WIP domains are unavailable/reserved with clean diagnostics;
- [ ] all green-list domains are implemented, documented, and tested;
- [ ] every implemented member has centralized signature metadata;
- [ ] every implemented native-backed member validates against native registry metadata;
- [ ] all examples use canonical `System.*` / `Standard.*` imports;
- [ ] full suite is green;
- [ ] JIT suite is green;
- [ ] website and docs match the released library surface;
- [ ] release install verifies `svm --version`, `svm check`, and representative `svm run` fixtures.
