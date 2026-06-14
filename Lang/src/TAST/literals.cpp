#include "TAST/literals.h"

#include "TAST/types.h"

namespace Simple::Lang::TAST {

bool IsLiteralCompatibleWithType(const Simple::Lang::AST::Expr& expr,
                                 const Simple::Lang::AST::TypeRef& expected) {
  if (expr.kind != ExprKind::Literal || !IsScalarType(expected)) return false;
  switch (expr.literal_kind) {
    case LiteralKind::Integer:
      return IsIntegerTypeName(expected.name) || IsFloatTypeName(expected.name);
    case LiteralKind::Float:
      return IsFloatTypeName(expected.name);
    case LiteralKind::String:
      return IsStringTypeName(expected.name);
    case LiteralKind::Char:
      return expected.name == "char" || IsIntegerTypeName(expected.name);
    case LiteralKind::Bool:
      return IsBoolTypeName(expected.name);
  }
  return false;
}

bool IsLiteralCompatibleWithScalarType(const Simple::Lang::AST::Expr& expr,
                                       const Simple::Lang::AST::TypeRef& expected) {
  if (!IsScalarType(expected)) return false;
  if (expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Integer &&
      IsIntegerScalarTypeName(expected.name)) {
    return true;
  }
  if (expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Float &&
      IsFloatScalarTypeName(expected.name)) {
    return true;
  }
  return false;
}

bool TypesCompatibleForExpr(const Simple::Lang::AST::TypeRef& expected,
                            const Simple::Lang::AST::TypeRef& actual,
                            const Simple::Lang::AST::Expr& expr) {
  if (TypeEquals(expected, actual)) return true;
  if (expected.pointer_depth == 0 && actual.pointer_depth == 0 &&
      !expected.is_proc && !actual.is_proc &&
      expected.name == actual.name &&
      TypeArgsEqual(expected.type_args, actual.type_args) &&
      expected.dims.size() == actual.dims.size()) {
    bool dims_ok = true;
    for (size_t i = 0; i < expected.dims.size(); ++i) {
      if (expected.dims[i].is_list != actual.dims[i].is_list) {
        dims_ok = false;
        break;
      }
      if (expected.dims[i].is_list) continue;
      if (!expected.dims[i].has_size) continue;
      if (!actual.dims[i].has_size || expected.dims[i].size != actual.dims[i].size) {
        dims_ok = false;
        break;
      }
    }
    if (dims_ok) return true;
  }
  return actual.pointer_depth == 0 && !actual.is_proc && actual.type_args.empty() &&
         actual.dims.empty() && IsLiteralCompatibleWithScalarType(expr, expected);
}

bool IsListLiteralExpr(const Simple::Lang::AST::Expr& expr) {
  return expr.kind == ExprKind::ListLiteral;
}

bool IsPositionalBraceLiteralExpr(const Simple::Lang::AST::Expr& expr) {
  if (expr.kind == ExprKind::ArrayLiteral) return true;
  if (expr.kind != ExprKind::ArtifactLiteral) return false;
  return expr.field_names.empty() && expr.field_values.empty();
}

bool InferLiteralType(const Simple::Lang::AST::Expr& expr,
                      const Simple::Lang::AST::TypeRef* expected,
                      Simple::Lang::AST::TypeRef* out,
                      std::string* error) {
  if (!out) return false;
  if (expr.kind != ExprKind::Literal) {
    if (error) *error = "expected literal expression";
    return false;
  }
  if (expected) {
    if (!IsLiteralCompatibleWithType(expr, *expected)) {
      if (error) *error = "literal is not compatible with expected type";
      return false;
    }
    return CloneTypeRef(*expected, out);
  }

  out->pointer_depth = 0;
  out->type_args.clear();
  out->dims.clear();
  out->is_proc = false;
  out->proc_params.clear();
  out->proc_return.reset();
  switch (expr.literal_kind) {
    case LiteralKind::Integer:
      out->name = "i32";
      return true;
    case LiteralKind::Float:
      out->name = "f64";
      return true;
    case LiteralKind::String:
      out->name = "string";
      return true;
    case LiteralKind::Char:
      out->name = "char";
      return true;
    case LiteralKind::Bool:
      out->name = "bool";
      return true;
  }
  if (error) *error = "unknown literal kind";
  return false;
}

} // namespace Simple::Lang::TAST
