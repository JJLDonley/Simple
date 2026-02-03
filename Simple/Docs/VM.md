# Simple::VM

**Scope**  
Runtime execution engine, heap/GC, JIT, and runtime diagnostics.

**How It Works**  
The VM executes verified SBC bytecode using an untagged slot runtime. Types are enforced by the
verifier and metadata; runtime slots are raw values. Heap objects (strings/arrays/lists/closures)
are managed by GC, and tiered JIT can optimize hot methods.

**What Works (Current)**  
- Interpreter, verifier integration, and untagged slot runtime.  
- Heap objects and GC safepoints with ref bitmaps/stack maps.  
- Tiered JIT path and runtime trap diagnostics.  

**Implementation Plan**  
1) Core runtime + call frames.  
2) Heap + GC root tracking.  
3) Extended opcodes (objects/arrays/lists/strings).  
4) Tiered JIT + diagnostics.  

**Future Plan**  
- Optional GC enhancements (arena/young/old) after v0.1 stability.  
- Additional JIT optimizations as workloads demand.  
