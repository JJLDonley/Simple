#include "TAST/expressions.h"

#include "TAST/types.h"

namespace Simple::Lang::TAST {

bool RequireScalar(const TypeRef& type, const std::string& op, std::string* error) {
  if (!IsScalarType(type)) {
    if (error) *error = "operator '" + op + "' requires scalar operands";
    return false;
  }
  return true;
}

bool CheckCompoundAssignOp(const std::string& op,
                           const TypeRef& lhs,
                           const TypeRef& rhs,
                           std::string* error) {
  if (!RequireScalar(lhs, op, error)) return false;
  if (!RequireScalar(rhs, op, error)) return false;
  if (!TypeEquals(lhs, rhs)) {
    if (error) *error = "assignment type mismatch";
    return false;
  }
  if (op == "&&" || op == "||") {
    if (!IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires bool operands";
      return false;
    }
    return true;
  }
  if (op == "==" || op == "!=") {
    if (IsStringTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' does not support string operands";
      return false;
    }
    if (!IsNumericTypeName(lhs.name) && !IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric or bool operands";
      return false;
    }
    return true;
  }
  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }
  if (op == "+" || op == "-" || op == "*" || op == "/") {
    if (!IsNumericTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operands";
      return false;
    }
    return true;
  }
  if (op == "%") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '%' requires integer operands";
      return false;
    }
    return true;
  }
  if (op == "<<" || op == ">>" || op == "&" || op == "|" || op == "^") {
    if (!IsIntegerTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires integer operands";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckExpressionShape(const Expr& expr, std::string* error) {
  if (expr.kind == Simple::Lang::AST::ExprKind::Identifier && expr.text.empty()) {
    if (error) *error = "identifier expression missing name";
    return false;
  }
  return true;
}

} // namespace Simple::Lang::TAST
