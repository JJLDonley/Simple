# Simple::Byte (Authoritative)

`Simple::Byte` defines the SBC binary contract and verification layer.

## What SBC Means

SBC is the executable bytecode format consumed by the VM.

Goals:

- deterministic binary layout
- strict table references
- verifier-enforced control-flow and type safety

## SBC Data Model

Primary structures are defined in `Byte/include/sbc_types.h`.

Core parts:

- `SbcHeader`
- section table (`SectionEntry[]`)
- metadata tables (types/sigs/methods/functions/globals/imports/exports)
- const pool
- code bytes

## Binary Layout

1. Header (`SBC0`, version, endian, section metadata)
2. Section table
3. Sections (aligned, non-overlapping)

Common sections:

- `types`
- `fields`
- `methods`
- `sigs`
- `globals`
- `functions`
- `imports`
- `exports`
- `const_pool`
- `code`
- `debug` (optional)

## Loader Meaning

Loader (`Byte/src/sbc_loader.cpp`) is responsible for:

- structural validation (bounds, alignment, overlaps)
- row-size/count validation per section
- cross-table reference validation
- const pool offset/type validation
- deterministic error messages on failure

## Verifier Meaning

Verifier (`Byte/src/sbc_verifier.cpp`) is responsible for:

- instruction boundary correctness
- jump target validity
- stack discipline and merge compatibility
- call/indirect/tailcall signature checks
- local/global initialization and type checks

## TypeKinds

SBC encodes type kinds used by verifier/runtime contracts:

- ints: `i8..i128`, `u8..u128`
- floats: `f32`, `f64`
- scalar misc: `bool`, `char`
- references: `string`, `ref`
- `unspecified` (special metadata role)

## Opcode Families (Compacted)

Canonical enum is in `Byte/include/opcode.h`.

Grouped overview:

| Family | Shape |
|---|---|
| Control flow | `jmp`, `jmp_true`, `jmp_false`, `jmp_table`, `ret`, `halt`, `trap` |
| Stack | `pop`, `dup`, `dup2`, `swap`, `rot` |
| Constants | `const_<T>`, `const_string`, `const_null` |
| Local/global/upvalue | `load_*`, `store_*` |
| Numeric arithmetic | `add_<T>`, `sub_<T>`, `mul_<T>`, `div_<T>`, `mod_<T>` |
| Unary numeric | `neg_<T>`, `inc_<T>`, `dec_<T>` |
| Comparisons | `cmp_<op>_<T>` |
| Bool logic | `bool_not`, `bool_and`, `bool_or` |
| Calls | `call`, `call_indirect`, `tailcall`, `enter`, `leave` |
| Conversions | `conv_<from>_to_<to>` |
| Objects/refs | `new_object`, `new_closure`, `load_field`, `store_field`, `is_null`, `ref_eq`, `ref_ne`, `type_of` |
| Arrays/lists | `new_array_<T>`, `array_get_<T>`, `array_set_<T>`, `new_list_<T>`, `list_get_<T>`, `list_set_<T>`, push/pop/insert/remove |
| String ops | `string_len`, `string_concat`, `string_get_char`, `string_slice` |
| Runtime hooks | `intrinsic`, `sys_call` |
| Debug/profile | `line`, `profile_start`, `profile_end` |

`<T>` indicates a type-specialized lane (for example `i32`, `i64`, `f32`, `f64`, `ref`).

## Invariants You Can Rely On

- invalid modules fail load/verify before VM execution
- signature mismatch is rejected, not coerced
- branch targets must hit valid instruction boundaries
- table IDs and const offsets must be valid

## Ownership

- Loader: `Byte/src/sbc_loader.cpp`
- Verifier: `Byte/src/sbc_verifier.cpp`
- Opcode metadata: `Byte/src/opcode.cpp`
