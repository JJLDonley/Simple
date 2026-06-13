#pragma once

#include <string>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

bool IsMutable(Simple::Lang::Mutability mutability);
bool CheckMutableAssignment(Simple::Lang::Mutability mutability,
                            std::string* error);

} // namespace Simple::Lang::TAST
