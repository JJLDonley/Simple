#include "TAST/calls.h"

namespace Simple::Lang::TAST {

bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error) {
  if (!type || !type->is_proc) return false;
  if (type->proc_params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch: expected " +
               std::to_string(type->proc_params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckCallExpression(const Expr& expr, std::string* error) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Call) {
    if (error) *error = "expected call expression";
    return false;
  }
  if (expr.children.empty()) {
    if (error) *error = "call expression missing callee";
    return false;
  }
  return true;
}

} // namespace Simple::Lang::TAST
