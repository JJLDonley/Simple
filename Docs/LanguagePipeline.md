# Language Pipeline Ownership

## Owned files
- Lexer: `Lang/include/Lexer/`, `Lang/src/Lexer/`
- CAST: `Lang/include/CAST/`, `Lang/src/CAST/`
- AST: `Lang/include/AST/`, `Lang/src/AST/`
- RAST: `Lang/include/RAST/`, `Lang/src/RAST/`
- TAST: `Lang/include/TAST/`, `Lang/src/TAST/`
- IRB: `Lang/include/IRB/`, `Lang/src/IRB/`
- IRE: `Lang/include/IRE/`, `Lang/src/IRE/`

## Forbidden dependencies
- Lexer and CAST must not depend on semantic phases.
- RAST owns names/symbols/imports/member references only.
- TAST owns types, mutability, ABI facts, and control-flow type facts.
- IRB must consume TAST, not raw parser state.
- IRE must consume IRB modules.

## Public API
- `Simple::Lang::CAST::ParseProgramFromString`
- `Simple::Lang::AST::LowerCastProgram`
- `Simple::Lang::RAST::ResolveProgram`
- `Simple::Lang::TAST::CheckResolvedProgram`
- `Simple::Lang::IRB::BuildModule`
- `Simple::Lang::IRE::EmitSirModule`

## Tests
- Split language tests live under `Tests/tests/lang/`.
