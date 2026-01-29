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
