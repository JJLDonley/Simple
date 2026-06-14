#include "TAST/types.h"

#include <memory>
#include <unordered_set>
#include <utility>

namespace Simple::Lang::TAST {

namespace {

const std::unordered_set<std::string> kPrimitiveTypes = {
  "i8", "i16", "i32", "i64",
  "u8", "u16", "u32", "u64",
  "f32", "f64",
  "bool", "char", "string",
};

} // namespace

bool CloneTypeRef(const Simple::Lang::AST::TypeRef& src,
                  Simple::Lang::AST::TypeRef* out) {
  if (!out) return false;
  out->name = src.name;
  out->pointer_depth = src.pointer_depth;
  out->type_args.clear();
  out->dims = src.dims;
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
  if (a.is_proc != b.is_proc) return false;
  if (a.is_proc) {
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

bool IsIntegerScalarTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64";
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

bool IsScalarType(const Simple::Lang::AST::TypeRef& type) {
  return type.pointer_depth == 0 &&
         !type.is_proc &&
         type.dims.empty() &&
         type.type_args.empty();
}

} // namespace Simple::Lang::TAST
