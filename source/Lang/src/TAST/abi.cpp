#include "TAST/abi.h"

#include "TAST/types.h"

namespace Simple::Lang::TAST {
namespace {

bool IsAbiScalar(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
         name == "isize" || name == "usize" || name == "f32" || name == "f64";
}

bool IsExternalNullablePointer(const Simple::Lang::AST::TypeRef& type) {
  const auto* value = OptionalValueType(type);
  return value && value->pointer_depth > 0 && value->dims.empty() &&
         value->type_args.empty();
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
  if (!artifact || !artifact->is_data || !artifact->generics.empty()) {
    visiting->erase(name);
    return false;
  }
  for (const auto& field : artifact->fields) {
    if (!field.type.type_args.empty() || !field.type.dims.empty() ||
        IsOptionalType(field.type)) {
      visiting->erase(name);
      return false;
    }
    if (field.type.pointer_depth > 0) continue;
    if (field.type.is_proc || !IsAbiScalar(field.type.name)) {
      if (artifacts.find(field.type.name) == artifacts.end()) {
        visiting->erase(name);
        return false;
      }
    }
    if (IsAbiScalar(field.type.name)) continue;
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
  if (!type.dims.empty()) return false;
  if (IsExternalNullablePointer(type)) return true;
  auto concrete_optional = artifacts.find(type.name);
  if (concrete_optional != artifacts.end() &&
      concrete_optional->second->tagged_kind == TaggedArtifactKind::Optional) {
    for (const auto& field : concrete_optional->second->fields) {
      if (field.name == "value" && field.type.pointer_depth > 0) return true;
    }
  }
  if (!type.type_args.empty()) return false;
  if (type.pointer_depth > 0) {
    if (type.is_proc) {
      if (type.pointer_depth != 1 || !type.proc_return) return false;
      if (!IsSupportedDlAbiType(*type.proc_return, enum_types, artifacts, true)) return false;
      for (const auto& param : type.proc_params) {
        if (!IsSupportedDlAbiType(param, enum_types, artifacts, false)) return false;
      }
      return true;
    }
    return type.name == "void" || IsAbiScalar(type.name) ||
           (artifacts.find(type.name) != artifacts.end() && artifacts.at(type.name)->is_data);
  }
  if (type.is_proc) return false;
  if (allow_void && type.name == "void") return true;
  if (IsAbiScalar(type.name)) return true;
  if (artifacts.find(type.name) != artifacts.end()) {
    std::unordered_set<std::string> visiting;
    return IsSupportedDlAbiArtifact(type.name, enum_types, artifacts, &visiting);
  }
  return false;
}

bool CheckExternAbiType(
    const Simple::Lang::AST::TypeRef& type,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::ArtifactDecl*>& artifacts,
    bool allow_void,
    const std::string& error_message,
    std::string* error) {
  if (!IsSupportedDlAbiType(type, enum_types, artifacts, allow_void)) {
    if (error) *error = error_message;
    return false;
  }
  return true;
}

bool CheckDlDynamicSignature(
    const Simple::Lang::AST::ExternDecl& ext,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::ArtifactDecl*>& artifacts,
    std::string* error) {
  if (!IsSupportedDlAbiType(ext.return_type, enum_types, artifacts, true)) {
    if (error) {
      *error = "dynamic DL return type for '" + ext.module + "." + ext.name +
               "' is not ABI-supported";
    }
    return false;
  }
  for (const auto& param : ext.params) {
    if (!IsSupportedDlAbiType(param.type, enum_types, artifacts, false)) {
      if (error) {
        *error = "dynamic DL parameter type for '" + ext.module + "." + ext.name +
                 "' is not ABI-supported";
      }
      return false;
    }
  }
  if (ext.params.size() > 254) {
    if (error) {
      *error = "dynamic DL symbol '" + ext.module + "." + ext.name +
               "' currently supports up to 254 ABI parameters";
    }
    return false;
  }
  return true;
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
    case Simple::Byte::TypeKind::ISize:
      *out = MakeSimpleType("isize");
      return true;
    case Simple::Byte::TypeKind::USize:
      *out = MakeSimpleType("usize");
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
    case Simple::Byte::TypeKind::Ptr:
      *out = MakeSimpleType("void");
      out->pointer_depth = 1;
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
  if (IsExternalNullablePointer(type)) return true;
  if (!type.dims.empty() || (!type.type_args.empty() && !IsExternalNullablePointer(type))) {
    if (error) *error = "extern ABI type shape is not supported";
    return false;
  }
  if (type.pointer_depth > 0) return true;
  if (type.is_proc) {
    if (error) *error = "VM procedure value is not an external function pointer";
    return false;
  }
  if (IsAbiScalar(type.name)) return true;
  if (error) *error = "extern ABI scalar type is not supported";
  return false;
}

} // namespace Simple::Lang::TAST
