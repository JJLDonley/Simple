# Lang Include Migration Policy

The language front-end is split into phase include directories. New code should prefer phase headers and treat legacy `lang_*.h` headers as compatibility facades.

| Legacy header | Preferred header |
|---|---|
| `lang_ast.h` | `AST/ast.h` for normalized AST types or `CAST/cast.h` for parser-tree aliases |
| `lang_parser.h` | `CAST/parser.h` |
| `lang_validate.h` | `TAST/type_checker.h` plus `RAST/resolver.h` for phase-aware semantic code |
| `lang_sir.h` | `IRE/sir_emitter.h` for SIR emission from IRB/typed modules |

Policy:

- Existing embedders may continue using the legacy headers during the v1 compatibility window.
- New compiler code should include phase headers directly.
- Legacy headers remain source-compatible facades until the phase split is complete and a major compatibility version bump is announced.
- Removing a legacy header requires a migration note and replacement test coverage.
