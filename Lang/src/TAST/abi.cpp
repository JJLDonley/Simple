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

bool IsSupportedDlAbiArtifact(
    const std::string& name,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::ArtifactDecl*>& artifacts,
    std::unordered_set<std::string>* visiting) {
  if (!visiting || !visiting->insert(name).second) return false;
  auto it = artifacts.find(name);
  if (it == artifacts.end()) {
    visiting->erase(name);
    return false;
  }
  const Simple::Lang::AST::ArtifactDecl* artifact = it->second;
  if (!artifact) {
    visiting->erase(name);
    return false;
  }
  for (const auto& field : artifact->fields) {
    if (field.type.is_proc || !field.type.type_args.empty() || !field.type.dims.empty()) {
      visiting->erase(name);
      return false;
    }
    if (field.type.pointer_depth > 0) continue;
    if (IsAbiScalar(field.type.name)) continue;
    if (enum_types.find(field.type.name) != enum_types.end()) continue;
    if (artifacts.find(field.type.name) != artifacts.end()) {
      if (!IsSupportedDlAbiArtifact(field.type.name, enum_types, artifacts, visiting)) {
        visiting->erase(name);
        return false;
      }
      continue;
    }
    visiting->erase(name);
    return false;
  }
  visiting->erase(name);
  return true;
}

} // namespace

bool IsSupportedDlAbiType(
    const Simple::Lang::AST::TypeRef& type,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::ArtifactDecl*>& artifacts,
    bool allow_void) {
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (allow_void && type.name == "void") return true;
  if (IsAbiScalar(type.name)) return true;
  if (enum_types.find(type.name) != enum_types.end()) return true;
  if (artifacts.find(type.name) != artifacts.end()) {
    std::unordered_set<std::string> visiting;
    return IsSupportedDlAbiArtifact(type.name, enum_types, artifacts, &visiting);
  }
  return false;
}

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
