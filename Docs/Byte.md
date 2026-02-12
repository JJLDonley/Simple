# Simple::Byte (API)

`Simple::Byte` defines the SBC binary contract and the loader/verifier behavior.

## Supported
- SBC binary layout with header, section table, aligned sections, and code bytes.
- Core sections: `types`, `fields`, `methods`, `sigs`, `globals`, `functions`, `imports`, `exports`, `const_pool`, `code`.
- Optional `debug` section (accepted but not required for execution).
- Loader structural validation: bounds, alignment, overlap checks, and row-size/count validation.
- Cross-table reference validation across types/sigs/methods/functions/globals/imports/exports.
- Const pool offset/type validation (strings, i128/u128 blobs, etc.).
- Verifier enforcement:
  - instruction boundaries and jump targets
  - stack discipline + merge compatibility
  - call/indirect/tailcall signature checks
  - local/global initialization and type checks
- Canonical opcode metadata defined in `Byte/include/opcode.h`.

## Not Supported
- Any forward/backward compatibility guarantee across SBC versions (policy not frozen yet).
- Execution of modules that fail load/verify (they are rejected before VM execution).

## Planned
- Formal SBC compatibility/versioning policy.
- Explicit forward/backward compatibility expectations for SBC consumers.
- Compatibility smoke tests using archived SBC fixtures.

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

## Loader Contract
Loader (`Byte/src/sbc_loader.cpp`) responsibilities:
- structural validation (bounds, alignment, overlaps)
- row-size/count validation per section
- cross-table reference validation
- const pool offset/type validation
- deterministic error messages on failure

## Verifier Contract
Verifier (`Byte/src/sbc_verifier.cpp`) responsibilities:
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
- Invalid modules fail load/verify before VM execution.
- Signature mismatch is rejected, not coerced.
- Branch targets must hit valid instruction boundaries.
- Table IDs and const offsets must be valid.

## Ownership
- Loader: `Byte/src/sbc_loader.cpp`
- Verifier: `Byte/src/sbc_verifier.cpp`
- Opcode metadata: `Byte/src/opcode.cpp`
