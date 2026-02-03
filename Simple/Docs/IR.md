# Simple::IR

**Scope**  
SIR text format and IR compiler that lowers SIR → SBC.

**How It Works**  
SIR is a typed, text‑based IR with optional metadata tables for name resolution. The IR compiler
parses SIR, validates types/labels/metadata, and emits SBC via the bytecode emitter.

**What Works (Current)**  
- SIR grammar + parsing.  
- Metadata tables (types/sigs/consts/imports/globals/upvalues).  
- Name resolution + validation and SBC emission.  
- Line‑aware diagnostics and perf harness for .sir programs.  

**Implementation Plan**  
1) Parser + tokenizer.  
2) Metadata tables + name resolution.  
3) Validation + lowering to SBC.  
4) Diagnostics and perf harness.  

**Future Plan**  
- Optional authoring ergonomics (includes/macros) if needed.  
- Additional metadata conveniences only if VM requires it.  
