#include "TAST/expressions.h"

#include "TAST/literals.h"
#include "TAST/types.h"

namespace Simple::Lang::TAST {

bool IsAddressableExpr(const Expr& expr) {
  if (expr.kind == Simple::Lang::AST::ExprKind::Identifier) return true;
  if (expr.kind == Simple::Lang::AST::ExprKind::Member && expr.op == "." &&
      expr.children.size() == 1) {
    return IsAddressableExpr(expr.children[0]);
  }
  return false;
}

bool IsMemberAccessExpr(const Expr& expr,
                        const Expr** out_base,
                        bool* out_is_pointer_access) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Member ||
      (expr.op != "." && expr.op != "->") || expr.children.empty()) {
    return false;
  }
  if (out_base) *out_base = &expr.children[0];
  if (out_is_pointer_access) *out_is_pointer_access = expr.op == "->";
  return true;
}

bool IsUnaryExpr(const Expr& expr, const Expr** out_operand) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Unary || expr.children.empty()) return false;
  if (out_operand) *out_operand = &expr.children[0];
  return true;
}

bool IsBinaryExpr(const Expr& expr,
                  const Expr** out_lhs,
                  const Expr** out_rhs) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Binary || expr.children.size() < 2) return false;
  if (out_lhs) *out_lhs = &expr.children[0];
  if (out_rhs) *out_rhs = &expr.children[1];
  return true;
}

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
      return true;
    }
    if (!IsNumericTypeName(lhs.name) && !IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric, bool, or string operands";
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

bool CheckUnaryOpTypeRules(const std::string& expr_op,
                           const TypeRef& operand,
                           const Expr& operand_expr,
                           std::string* error) {
  const std::string op = expr_op.rfind("post", 0) == 0 ? expr_op.substr(4) : expr_op;
  if (op == "&") {
    if (!IsAddressableExpr(operand_expr)) {
      if (error) *error = "address-of requires assignable expression";
      return false;
    }
    return true;
  }
  if (op == "*") {
    if (operand.pointer_depth == 0) {
      if (error) *error = "dereference requires pointer operand";
      return false;
    }
    if (operand.name == "void" && operand.pointer_depth == 1) {
      if (error) *error = "cannot dereference void pointer";
      return false;
    }
    return true;
  }
  if (op == "?") return true;
  if (op == "await") {
    if (operand.name != "Promise" || operand.type_args.size() != 1 ||
        operand.pointer_depth != 0 || !operand.dims.empty()) {
      if (error) *error = "await requires Promise<T> operand";
      return false;
    }
    return true;
  }
  if (!RequireScalar(operand, expr_op, error)) return false;
  if (op == "!") {
    if (!IsBoolTypeName(operand.name)) {
      if (error) *error = "operator '!' requires bool operand";
      return false;
    }
    return true;
  }
  if (op == "++" || op == "--" || op == "-") {
    if (!IsNumericTypeName(operand.name)) {
      if (error) *error = "operator '" + op + "' requires numeric operand";
      return false;
    }
    return true;
  }
  return true;
}

bool CheckBinaryOpTypeRules(const std::string& op,
                            const TypeRef& lhs,
                            const TypeRef& rhs,
                            const Expr& lhs_expr,
                            const Expr& rhs_expr,
                            std::string* error) {
  if (lhs.pointer_depth > 0 || rhs.pointer_depth > 0) {
    if (op != "==" && op != "!=") {
      if (error) *error = "pointers support only equality comparisons";
      return false;
    }
    if (!TypeEquals(lhs, rhs)) {
      if (error) *error = "pointer comparison requires matching pointer types";
      return false;
    }
    return true;
  }
  if (!RequireScalar(lhs, op, error)) return false;
  if (!RequireScalar(rhs, op, error)) return false;
  if (!TypeEquals(lhs, rhs)) {
    if (!IsLiteralCompatibleWithScalarType(lhs_expr, rhs) &&
        !IsLiteralCompatibleWithScalarType(rhs_expr, lhs)) {
      if (error) *error = "operator '" + op + "' requires matching operand types";
      return false;
    }
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
      return true;
    }
    if (!IsNumericTypeName(lhs.name) && !IsBoolTypeName(lhs.name)) {
      if (error) *error = "operator '" + op + "' requires numeric, bool, or string operands";
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
