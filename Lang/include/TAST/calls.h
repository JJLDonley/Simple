#pragma once

#include <string>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool CheckCallExpression(const Expr& expr, std::string* error);

} // namespace Simple::Lang::TAST
