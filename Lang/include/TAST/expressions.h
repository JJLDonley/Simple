#pragma once

#include <string>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool IsAddressableExpr(const Expr& expr);
bool RequireScalar(const TypeRef& type, const std::string& op, std::string* error);
bool CheckCompoundAssignOp(const std::string& op,
                           const TypeRef& lhs,
                           const TypeRef& rhs,
                           std::string* error);
bool CheckExpressionShape(const Expr& expr, std::string* error);

} // namespace Simple::Lang::TAST
