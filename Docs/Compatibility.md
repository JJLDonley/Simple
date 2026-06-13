# Simple Compatibility Versions

The project exposes explicit compatibility version constants for generated artifacts and native integrations.

| Surface | Constant | Current |
|---|---|---|
| Lang syntax | `Simple::Lang::kLangSyntaxVersionMajor/Minor` | `1.0` |
| SIR text | `Simple::Lang::kSirVersionMajor/Minor` | `1.0` |
| SBC binary | `Simple::Byte::kSbcVersion` | `0x0001` |
| Opcode metadata | `Simple::Byte::kOpcodeMetadataVersion` | `1` |
| Runtime ABI | `Simple::VM::kRuntimeAbiVersionMajor/Minor` | `1.0` |
| Stdlib modules | `Simple::Lang::kStdlibVersionMajor/Minor` | `1.0` |

Major version changes may break external producers/embedders. Minor version changes are additive within the same major version.
