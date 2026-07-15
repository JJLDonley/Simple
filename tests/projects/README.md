# Timeline-gated conformance projects

This directory contains realistic application-level regression projects. Projects
are activated only when their required timeline feature gate is implemented.
They complement focused tests and `tests/simple_stress`; they do not replace them.

Rules:

- one canonical project source for interpreter and JIT execution;
- deterministic automated state/output checks;
- no future API mocks, compatibility shims, or reduced duplicate implementations;
- manual graphics/input checks separated from deterministic simulation tests;
- each project includes `PROJECT.md` with its feature matrix and expected results;
- native test assets remain under `_vendor/` and are never packaged as runtime dependencies.

The local canonical backlog is `.notes/TestProjects.md`. The first project activated
for the completed closure gate is the multi-module Text-Based Adventure Game.

Graphics projects use raylib 6.0 from `_vendor/raylib/6.0/`:

- Linux x86-64
- macOS universal x86-64/arm64
- Windows x86-64

`playground/` is personal workspace and is not used by automated conformance tests.
