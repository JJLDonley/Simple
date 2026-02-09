# Simple::VM (Authoritative)

`Simple::VM` executes verified SBC modules and hosts runtime services.

## Runtime Purpose

- deterministic interpreter execution
- strict slot/type expectations enforced by verifier contracts
- heap + object model + GC
- core import module dispatch
- dynamic library interop via `DL`

## Execution Model

`ExecuteModule` flow:

1. optional verifier gate
2. global initialization
3. frame + stack setup
4. interpreter loop
5. `ExecResult` with status/exit/diagnostics

Primary implementation: `VM/src/vm.cpp`.

## Slot And Frame Model

- stack/locals/globals use 64-bit slots
- ref null sentinel: `0xFFFFFFFF`
- call frame tracks function index, return pc, local range, stack base
- supports direct call, indirect call, and tailcall

## Heap/Object Model

Kinds include:

- string
- array
- list
- artifact
- closure

Heap implementation: `VM/src/heap.cpp`.

## Core Runtime Library Surface

Runtime import dispatch supports:

- `core.io`
- `core.fs`
- `core.os`
- `core.log`
- `core.dl`

See full API tables in `Docs/StdLib.md`.

## DLL / C-C++ Interop Path

`DL` path:

1. open dynamic library
2. load symbol
3. bind call signature from `extern` manifest metadata
4. marshal VM values -> native ABI values
5. call native symbol
6. marshal native return -> VM value

Current ABI backend is libffi-driven on supported platforms.

Supported shapes:

- scalar numeric/char/bool
- pointers (`*T`, `*void`)
- enums
- artifacts by value

Known limitation:

- recursive artifact struct ABI is rejected

## Import + Extern For Static Files

- `import` resolves reserved modules and project files (see `Docs/Lang.md`)
- `extern` defines typed native symbol signatures used by `DL`
- runtime does not guess signature layouts; it uses declared metadata

## JIT Note

Interpreter is the canonical correctness path.
JIT/tier scaffolding exists, but runtime behavior must always be valid without relying on JIT.

## Ownership

- VM runtime: `VM/src/vm.cpp`
- Heap/GC: `VM/src/heap.cpp`
- Public headers: `VM/include/vm.h`, `VM/include/heap.h`
