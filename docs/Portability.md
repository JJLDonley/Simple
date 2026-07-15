# Portability architecture

Simple separates portable compiler/VM code from host integration under
`source/Platform`.

## Boundary

Portable code uses `platform/platform.h` for:

- host OS, architecture, process, and memory-page identification;
- executable and temporary-directory discovery;
- path-list delimiters, UTC conversion, and host-safe file opening;
- environment access;
- dynamic-library loading;
- native runner compilation.

CMake selects exactly one implementation from `src/linux`, `src/macos`, or
`src/windows`, plus architecture detection from `src/common`. OS API headers
and OS conditionals belong in those implementation files, not in VM or CLI
code.

Architecture is independent from operating system. Architecture-specific ABI
or code-generation behavior should live in a dedicated ABI/JIT component and
switch on `Platform::Architecture`; it must not create combined identities such
as `WindowsX64`.

## External ABI portability

The `v0.6` language contract keeps portable SBC values separate from host C ABI
marshaling. `usize`/`isize` carry pointer-width ABI intent and are range-checked
when lowered for the host; pointers are never represented in source as
`i64`/`u64`. Fixed-width Simple integers map only to matching fixed-width C
integers. Enums default to `i32` or declare an exact fixed-width integer
underlying type; that width and signedness are preserved in stable struct
layouts and external calls.

External nullable pointers use source `T*?`. The marshaler maps absent to C
address zero and nonzero C addresses to present pointers without exposing a
source null type. General tagged `T?`, Result, Promise, managed references, and
closures have no implicit external-C representation. Calling conventions,
aggregate layout, callback pointers, alignment, pointee mutability, provenance,
ownership, and lifetime must be validated for the selected host ABI before a
call executes.

Portable SBC does not encode a host address as a distributable constant. Raw
external pointers are runtime-local borrowed values and cannot survive artifact
serialization, process transfer, or migration to a different host ABI.

## Build rules

Platform CMake modules define generated-artifact names. Code must not assume
`.so`, `.dylib`, `.dll`, `.a`, or `.lib` names. Native builds use
`Platform::BuildNativeExecutable`, which respects `CXX` and selects the host
compiler/linker convention.

Tests use relative fixture paths or `std::filesystem::temp_directory_path()`.
They must not assume `/tmp`, `/proc`, Unix separators, or a particular shared
library extension.

## Adding a host

1. Add `source/Platform/src/<host>/platform.cpp` implementing the complete
   interface.
2. Add a `cmake/platform/<host>.cmake` selection and artifact names.
3. Add a CI package build and clean extracted-package smoke test.
4. Validate both interpreter-only and LLVM 18 configurations where supported.
