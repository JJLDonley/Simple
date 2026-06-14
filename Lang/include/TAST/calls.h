#pragma once

#include <cstddef>
#include <string>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool CheckCallExpression(const Expr& expr, std::string* error);
bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error);

} // namespace Simple::Lang::TAST
