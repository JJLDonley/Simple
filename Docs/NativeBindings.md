# Native Bindings Ownership

## Owned files
- Native registry: `VM/include/native/registry.h`, `VM/src/native/registry.cpp`
- Native modules: `VM/include/native/*.h`, `VM/src/native/*.cpp`
- Native metadata docs: `Docs/StdLib.md`

## Forbidden dependencies
- Native modules must not depend on CLI or LSP.
- VM dispatch should use native metadata instead of ad-hoc forwarding glue.
- New native functions must register metadata: module, symbol, parameters, result, handler.

## Public API
- `Simple::VM::Native::BuildDefaultRegistry`
- `Simple::VM::Native::NativeRegistry`
- `Simple::VM::Native::GenerateStdLibMarkdown`
- `Simple::VM::Native::NativeCallContext`

## Tests
- Native registry and metadata tests live in VM test sections.
- Split native tests live under `Tests/tests/vm/`.
