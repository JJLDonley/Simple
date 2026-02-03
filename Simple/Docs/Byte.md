# Simple::Byte

**Scope**  
SBC file format, encoding, metadata tables, opcode list, loader rules, and verifier rules.

**How It Works**  
The Byte module defines the SBC binary layout and validation rules. The loader parses headers,
sections, tables, heaps, and code; the verifier enforces structural and type safety before run/JIT.

**What Works (Current)**  
- Headers/sections/encoding rules implemented and validated.  
- Metadata tables + heaps decoded with strict bounds checks.  
- Verifier enforces stack/type safety and control‑flow validity.  

**Implementation Plan**  
1) Header + section table parsing/validation.  
2) Metadata + heap decoding.  
3) Verifier rules (stack/type/CF).  
4) Diagnostics and error context.  

**Future Plan**  
- Extend metadata only if new VM features require it.  
- ABI‑compatible changes only behind version bumps.  
