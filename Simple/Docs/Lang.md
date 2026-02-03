# Simple::Lang

**Scope**  
Language frontend(s) that target SIR.

**How It Works**  
Language compilers lower source → SIR text (or direct emitter API) and rely on the VM for
verification and execution.

**What Works (Current)**  
- Not implemented (intentional).

**Implementation Plan**  
1) Build Simple language front‑end targeting SIR.  
2) Layer standard library using ABI/FFI.  

**Future Plan**  
- Full language toolchain and packaging.  
