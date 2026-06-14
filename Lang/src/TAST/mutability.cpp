#include "TAST/mutability.h"

namespace Simple::Lang::TAST {

bool IsMutable(Simple::Lang::Mutability mutability) {
  return mutability == Simple::Lang::Mutability::Mutable;
}

bool IsAddressOfExpr(const Simple::Lang::AST::Expr& expr,
                     const Simple::Lang::AST::Expr** out_target) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Unary || expr.op != "&" || expr.children.empty()) {
    return false;
  }
  if (out_target) *out_target = &expr.children[0];
  return true;
}

bool IsIndexExpr(const Simple::Lang::AST::Expr& expr,
                 const Simple::Lang::AST::Expr** out_base) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Index || expr.children.empty()) return false;
  if (out_base) *out_base = &expr.children[0];
  return true;
}

bool CheckMutableAssignment(Simple::Lang::Mutability mutability,
                            std::string* error) {
  if (IsMutable(mutability)) return true;
  if (error) *error = "cannot assign to immutable value";
  return false;
}

} // namespace Simple::Lang::TAST
