#pragma once

#include <string>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

bool CheckAbiShape(const Simple::Lang::AST::TypeRef& type,
                   bool allow_void,
                   std::string* error);

} // namespace Simple::Lang::TAST
