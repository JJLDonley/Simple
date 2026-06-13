#include "TAST/literals.h"

namespace Simple::Lang::TAST {
namespace {

bool IsIntegerTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
         name == "char";
}

bool IsFloatTypeName(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsBoolTypeName(const std::string& name) {
  return name == "bool";
}

bool IsStringTypeName(const std::string& name) {
  return name == "string";
}

bool IsPlainScalar(const Simple::Lang::AST::TypeRef& type) {
  return !type.is_proc && type.pointer_depth == 0 && type.dims.empty() && type.type_args.empty();
}

bool CloneTypeRef(const Simple::Lang::AST::TypeRef& src,
                  Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  *out = src;
  return true;
}

} // namespace

bool IsLiteralCompatibleWithType(const Simple::Lang::AST::Expr& expr,
                                 const Simple::Lang::AST::TypeRef& expected) {
  if (expr.kind != ExprKind::Literal || !IsPlainScalar(expected)) return false;
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
