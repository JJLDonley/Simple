#include "TAST/types.h"

#include <memory>
#include <unordered_set>
#include <utility>

namespace Simple::Lang::TAST {

namespace {

const std::unordered_set<std::string> kPrimitiveTypes = {
  "i8", "i16", "i32", "i64",
  "u8", "u16", "u32", "u64", "usize", "isize",
  "f32", "f64",
  "bool", "char", "string",
};

} // namespace

Simple::Lang::AST::TypeRef MakeSimpleType(const std::string& name) {
  Simple::Lang::AST::TypeRef out;
  out.name = name;
  out.pointer_depth = 0;
  out.type_args.clear();
  out.dims.clear();
  out.is_proc = false;
  out.proc_params.clear();
  out.proc_return.reset();
  return out;
}

Simple::Lang::AST::TypeRef MakeListType(const std::string& name) {
  Simple::Lang::AST::TypeRef out = MakeSimpleType(name);
  Simple::Lang::AST::TypeDim dim;
  dim.is_list = true;
  dim.has_size = false;
  dim.size = 0;
  out.dims.push_back(dim);
  return out;
}

bool CloneTypeRef(const Simple::Lang::AST::TypeRef& src,
                  Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  out->name = src.name;
  out->pointer_depth = src.pointer_depth;
  out->type_args.clear();
  out->dims = src.dims;
  out->is_optional_syntax = src.is_optional_syntax;
  out->is_proc = src.is_proc;
  out->proc_return_mutability = src.proc_return_mutability;
  out->proc_params.clear();
  out->proc_return.reset();
  out->line = src.line;
  out->column = src.column;
  for (const auto& arg : src.type_args) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(arg, &copy)) return false;
    out->type_args.push_back(std::move(copy));
  }
  for (const auto& param : src.proc_params) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(param, &copy)) return false;
    out->proc_params.push_back(std::move(copy));
  }
  if (src.proc_return) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(*src.proc_return, &copy)) return false;
    out->proc_return = std::make_unique<Simple::Lang::AST::TypeRef>(std::move(copy));
  }
  return true;
}

bool CloneTypeVector(const std::vector<Simple::Lang::AST::TypeRef>& src,
                     std::vector<Simple::Lang::AST::TypeRef>* out) {
  if (!out) return false;
  out->clear();
  out->reserve(src.size());
  for (const auto& item : src) {
    Simple::Lang::AST::TypeRef copy;
    if (!CloneTypeRef(item, &copy)) return false;
    out->push_back(std::move(copy));
  }
  return true;
}

bool CloneElementType(const Simple::Lang::AST::TypeRef& container,
                      Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  if (container.dims.empty()) return false;
  if (!CloneTypeRef(container, out)) return false;
  out->dims.erase(out->dims.begin());
  return true;
}

bool TypeDimsEqual(const std::vector<Simple::Lang::AST::TypeDim>& a,
                   const std::vector<Simple::Lang::AST::TypeDim>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].is_list != b[i].is_list) return false;
    if (a[i].has_size != b[i].has_size) return false;
    if (a[i].has_size && a[i].size != b[i].size) return false;
  }
  return true;
}

bool TypeArgsEqual(const std::vector<Simple::Lang::AST::TypeRef>& a,
                   const std::vector<Simple::Lang::AST::TypeRef>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!TypeEquals(a[i], b[i])) return false;
  }
  return true;
}

bool TypeEquals(const Simple::Lang::AST::TypeRef& a,
                const Simple::Lang::AST::TypeRef& b) {
  if (a.pointer_depth != b.pointer_depth) return false;
  if (a.is_optional_syntax != b.is_optional_syntax) return false;
  if (a.is_proc != b.is_proc) return false;
  if (a.is_proc) {
    if (!TypeDimsEqual(a.dims, b.dims)) return false;
    if (a.proc_return_mutability != b.proc_return_mutability) return false;
    if (a.proc_params.size() != b.proc_params.size()) return false;
    for (size_t i = 0; i < a.proc_params.size(); ++i) {
      if (!TypeEquals(a.proc_params[i], b.proc_params[i])) return false;
    }
    if (!a.proc_return || !b.proc_return) return false;
    if (!TypeEquals(*a.proc_return, *b.proc_return)) return false;
    return true;
  }
  return a.name == b.name &&
         TypeArgsEqual(a.type_args, b.type_args) &&
         TypeDimsEqual(a.dims, b.dims);
}

bool IsPlainTypeRef(const Simple::Lang::AST::TypeRef& type) {
  return !type.is_proc && type.pointer_depth == 0 && type.dims.empty() && type.type_args.empty();
}

bool IsPrimitiveTypeName(const std::string& name) {
  return kPrimitiveTypes.find(name) != kPrimitiveTypes.end();
}

bool IsPrimitiveCastName(const std::string& name) {
  return IsPrimitiveTypeName(name);
}

bool GetAtCastTargetName(const std::string& name, std::string* out_target) {
  if (name.size() < 2 || name[0] != '@') return false;
  const std::string target = name.substr(1);
  if (!IsPrimitiveCastName(target)) return false;
  if (out_target) *out_target = target;
  return true;
}

bool CheckPrimitiveCastSyntaxName(const std::string& name, std::string* error) {
  if (IsPrimitiveCastName(name)) {
    if (error) *error = "primitive cast syntax requires '@': use @" + name + "(value)";
    return false;
  }
  return true;
}

bool IsBuiltinValueIdentifierName(const std::string& name) {
  return name == "len" || IsPrimitiveCastName(name) || GetAtCastTargetName(name, nullptr);
}

bool IsBuiltinCallIdentifierName(const std::string& name) {
  return name == "len" || GetAtCastTargetName(name, nullptr);
}

bool IsIntegerScalarTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
         name == "usize" || name == "isize";
}

bool IsIntegerTypeName(const std::string& name) {
  return IsIntegerScalarTypeName(name) || name == "char";
}

bool IsFloatScalarTypeName(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsFloatTypeName(const std::string& name) {
  return IsFloatScalarTypeName(name);
}

bool IsNumericScalarTypeName(const std::string& name) {
  return IsIntegerScalarTypeName(name) || IsFloatScalarTypeName(name);
}

bool IsBoolTypeName(const std::string& name) {
  return name == "bool";
}

bool IsStringTypeName(const std::string& name) {
  return name == "string";
}

bool IsNumericTypeName(const std::string& name) {
  return IsIntegerTypeName(name) || IsFloatTypeName(name);
}

bool IsListMethodName(const std::string& name) {
  return name == "len" || name == "push" || name == "pop" ||
         name == "insert" || name == "remove" || name == "clear";
}

bool CanonicalGenericTypeArity(const std::string& name, size_t* out_arity) {
  if (!out_arity) return false;
  if (name == "Result") {
    *out_arity = 2;
    return true;
  }
  if (name == kOptionalTypeInternalName || name == "Promise") {
    *out_arity = 1;
    return true;
  }
  return false;
}

bool IsOptionalType(const Simple::Lang::AST::TypeRef& type) {
  return type.name == kOptionalTypeInternalName && type.is_optional_syntax &&
         type.pointer_depth == 0 && type.dims.empty() && !type.is_proc &&
         type.type_args.size() == 1;
}

const Simple::Lang::AST::TypeRef* OptionalValueType(
    const Simple::Lang::AST::TypeRef& type) {
  return IsOptionalType(type) ? &type.type_args[0] : nullptr;
}

bool IsScalarType(const Simple::Lang::AST::TypeRef& type) {
  return type.pointer_depth == 0 &&
         !type.is_proc &&
         type.dims.empty() &&
         type.type_args.empty();
}

bool CheckKnownTypeName(const Simple::Lang::AST::TypeRef& type,
                        bool is_primitive,
                        bool is_type_param,
                        bool is_user_type,
                        std::string* error) {
  if (!is_primitive && !is_type_param && !is_user_type) {
    if (error) *error = "unknown type: " + type.name;
    return false;
  }
  return true;
}

bool CheckVoidTypeArgs(const Simple::Lang::AST::TypeRef& type, std::string* error) {
  if (!type.type_args.empty()) {
    if (error) *error = "void cannot have type arguments";
    return false;
  }
  return true;
}

bool CheckTypeArgumentRules(const Simple::Lang::AST::TypeRef& type,
                            bool is_primitive,
                            bool is_type_param,
                            bool is_enum_type,
                            const size_t* expected_aggregate_type_args,
                            std::string* error) {
  if (is_enum_type && !type.type_args.empty()) {
    if (error) *error = "enum type cannot have type arguments: " + type.name;
    return false;
  }
  if (expected_aggregate_type_args && type.type_args.size() != *expected_aggregate_type_args) {
    if (error) *error = "generic type argument count mismatch for " + type.name;
    return false;
  }
  if (!type.type_args.empty() && is_primitive) {
    if (error) *error = "primitive type cannot have type arguments: " + type.name;
    return false;
  }
  if (!type.type_args.empty() && is_type_param) {
    if (error) *error = "type parameter cannot have type arguments: " + type.name;
    return false;
  }
  return true;
}

bool IsLenCompatibleType(const Simple::Lang::AST::TypeRef& type) {
  return !type.dims.empty() || type.name == "string";
}

bool CheckPrimitiveCastArgType(const std::string& cast_target,
                               const Simple::Lang::AST::TypeRef& arg_type,
                               std::string* error) {
  if (arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
    if (error) *error = cast_target + " cast expects scalar argument";
    return false;
  }
  if (cast_target == "string") {
    if (!IsNumericTypeName(arg_type.name) && !IsBoolTypeName(arg_type.name)) {
      if (error) *error = "string cast expects numeric or bool argument";
      return false;
    }
  } else if (IsStringTypeName(arg_type.name) && !(cast_target == "i32" || cast_target == "f64")) {
    if (error) *error = cast_target + " cast from string is unsupported";
    return false;
  }
  return true;
}

} // namespace Simple::Lang::TAST
