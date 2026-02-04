# Simple::CLI

**Scope**  
Command‑line runners and developer tools (test runners, perf harness, SIR runner).

**How It Works**  
CLI entry points use the IR compiler and VM runtime to run SIR text and SBC modules, with
diagnostics and perf reporting.

**What Works (Current)**  
- Test runners and perf harness.  
- SIR runner for IR text → SBC → VM execution.  

**Implementation Plan**  
See `Simple/Docs/Implementation.md` (Module: Simple::CLI).
