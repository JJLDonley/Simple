#pragma once

// Legacy compatibility facade. New SIR-emission code should include
// "IRE/sir_emitter.h".

#include <string>

#include "AST/ast.h"
#include "simple_api.h"

namespace Simple::Lang {

SIMPLEVM_API bool EmitSir(const Program& program, std::string* out, std::string* error);
SIMPLEVM_API bool EmitSirFromString(const std::string& text, std::string* out, std::string* error);

} // namespace Simple::Lang
