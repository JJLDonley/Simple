# Diagnostics Ownership

## Owned files
- Structured diagnostics: `Lang/include/Diagnostics/diagnostic.h`, `Lang/src/Diagnostics/diagnostic.cpp`
- CLI rendering: `CLI/include/diagnostic_render.h`, `CLI/src/diagnostic_render.cpp`
- LSP bridge: `LSP/include/diagnostic_bridge.h`, `LSP/src/diagnostic_bridge.cpp`

## Forbidden dependencies
- Compiler phases should create structured diagnostics instead of formatting CLI/LSP output.
- CLI renders diagnostics for terminals only.
- LSP converts diagnostics to protocol JSON only.
- CLI and LSP must not duplicate compiler semantic checks.

## Public API
- `Simple::Lang::Diagnostics::Diagnostic`
- `Simple::Lang::Diagnostics::MakeDiagnostic`
- `Simple::Lang::Diagnostics::FormatDiagnostic`
- `Simple::CLI::RenderErrorLine`
- `Simple::LSP::PublishDiagnosticsMessage`

## Tests
- Diagnostic unit coverage is in language, CLI, and LSP test sections.
