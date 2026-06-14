#include "TAST/abi.h"

#include "TAST/types.h"

namespace Simple::Lang::TAST {
namespace {

bool IsAbiScalar(const std::string& name) {
  return name == "bool" || name == "char" || name == "i8" || name == "i16" ||
         name == "i32" || name == "i64" || name == "u8" || name == "u16" ||
         name == "u32" || name == "u64" || name == "f32" || name == "f64" ||
         name == "string";
}

} // namespace

bool NativeTypeToLangType(Simple::Byte::TypeKind kind,
                          Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  switch (kind) {
    case Simple::Byte::TypeKind::Bool:
      *out = MakeSimpleType("bool");
      return true;
    case Simple::Byte::TypeKind::I32:
      *out = MakeSimpleType("i32");
      return true;
    case Simple::Byte::TypeKind::I64:
      *out = MakeSimpleType("i64");
      return true;
    case Simple::Byte::TypeKind::F32:
      *out = MakeSimpleType("f32");
      return true;
    case Simple::Byte::TypeKind::F64:
      *out = MakeSimpleType("f64");
      return true;
    case Simple::Byte::TypeKind::String:
      *out = MakeSimpleType("string");
      return true;
    case Simple::Byte::TypeKind::Ref:
      *out = MakeListType("i32");
      return true;
    case Simple::Byte::TypeKind::Unspecified:
      *out = MakeSimpleType("void");
      return true;
    default:
      return false;
  }
}

bool CheckAbiShape(const Simple::Lang::AST::TypeRef& type,
                   bool allow_void,
                   std::string* error) {
  if (allow_void && type.name == "void" && type.pointer_depth == 0 && type.dims.empty() &&
      type.type_args.empty() && !type.is_proc) {
    return true;
  }
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) {
    if (error) *error = "extern ABI type shape is not supported";
    return false;
  }
  if (type.pointer_depth > 0) return true;
  if (IsAbiScalar(type.name)) return true;
  if (error) *error = "extern ABI scalar type is not supported";
  return false;
}

} // namespace Simple::Lang::TAST
