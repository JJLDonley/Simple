#pragma once

#include <string>

#include "AST/ast.h"
#include "IRB/ir_builder.h"
#include "simple_api.h"

namespace Simple::Lang::IRE {

SIMPLEVM_API bool EmitSir(const Simple::Lang::Program& program, std::string* out, std::string* error);
SIMPLEVM_API bool EmitSirFromString(const std::string& text, std::string* out, std::string* error);

bool EmitSirModule(const Simple::Lang::IRB::Module& module,
                   std::string* out,
                   std::string* error);

} // namespace Simple::Lang::IRE
