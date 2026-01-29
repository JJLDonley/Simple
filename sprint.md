# Sprint Log

## Phase 1

### Step 1 - 2026-01-29
- Initialized compiler folder structure: `compiler/` and `compiler/module/`.
- Created `sprint.md` for phase tracking.

### Step 2 - 2026-01-29
- Initialized compiler solution and projects under `compiler/`.
- Created projects: `Simple.Compiler.Module`, `Simple.Compiler`, `Simple.Compiler.Cli`, `Simple.Compiler.Tests`.
- Added project references between module/core/cli/tests.
- Noted: test project restore requires NuGet access; will run tests once packages are available.

### Step 3 - 2026-01-29
- Added `.gitignore` to exclude build artifacts.
- Removed tracked `obj/` folders from compiler projects.

### Step 4 - 2026-01-29
- Added core reusable module utilities: `SourceText`, `TextSpan`, `Diagnostic`, and `DiagnosticBag`.
- Removed placeholder class from module project.

### Step 5 - 2026-01-29
- Implemented token model and lexer with keyword/operator support.
- Added numeric, string, char literal parsing and comment handling.

### Step 6 - 2026-01-29
- Implemented syntax tree, parser, and AST node types for Phase 1 constructs.
- Added operator precedence table for expression parsing.

### Step 7 - 2026-01-29
- Implemented binder, symbols, and bound node model for Phase 1 type checking.
- Added built-in procedures (`print`, `println`) and basic operator binding.

### Step 8 - 2026-01-29
- Added compilation pipeline, CIL code generation, and CLI commands (build/check/run).
- Extended `SourceText` with line text/span helpers for diagnostics.
- Added reflection-based entry point handling and output persistence fallback.
- Note: CLI/tests builds require restore; dotnet restore currently failing in this environment.

### Step 9 - 2026-01-29
- Added unknown type diagnostics during binding.

### Step 10 - 2026-01-29
- Added `/compiler/NuGet.Config` to avoid external package sources for local builds.
- Restore still failing due to missing workload SDK resolver; needs environment fix to run CLI/tests.

### Step 11 - 2026-01-29
- Fixed lexer keyword test token count to match spec keywords list.
