#include "TAST/expressions.h"

namespace Simple::Lang::TAST {

bool CheckExpressionShape(const Expr& expr, std::string* error) {
  if (expr.kind == Simple::Lang::AST::ExprKind::Identifier && expr.text.empty()) {
    if (error) *error = "identifier expression missing name";
    return false;
  }
  return true;
}

} // namespace Simple::Lang::TAST
