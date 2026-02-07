#include "lang_validate.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "lang_parser.h"

namespace Simple::Lang {
namespace {

struct ValidateContext {
  std::unordered_set<std::string> enum_members;
  std::unordered_set<std::string> enum_types;
  std::unordered_map<std::string, std::unordered_set<std::string>> enum_members_by_type;
  std::unordered_set<std::string> top_level;
  std::unordered_map<std::string, const ArtifactDecl*> artifacts;
  std::unordered_map<std::string, size_t> artifact_generics;
  std::unordered_map<std::string, const ModuleDecl*> modules;
  std::unordered_map<std::string, const VarDecl*> globals;
  std::unordered_map<std::string, const FuncDecl*> functions;
  std::unordered_map<std::string, const ExternDecl*> externs;
  std::unordered_map<std::string, std::unordered_map<std::string, const ExternDecl*>> externs_by_module;
  std::unordered_set<std::string> reserved_imports;
  std::unordered_map<std::string, std::string> reserved_import_aliases;
};

struct LocalInfo {
  Mutability mutability = Mutability::Mutable;
  const TypeRef* type = nullptr;
  std::string dl_module;
};

struct CallTargetInfo {
  std::vector<TypeRef> params;
  TypeRef return_type;
  Mutability return_mutability = Mutability::Mutable;
  std::vector<std::string> type_params;
  bool is_proc = false;
};

void PrefixErrorLocation(uint32_t line, uint32_t column, std::string* error) {
  if (!error || error->empty() || line == 0) return;
  *error = std::to_string(line) + ":" + std::to_string(column) + ": " + *error;
}

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out);
bool IsIntegerLiteralExpr(const Expr& expr);
bool IsIntegerScalarTypeName(const std::string& name);
bool IsBoolTypeName(const std::string& name);
bool IsNumericTypeName(const std::string& name);
bool IsScalarType(const TypeRef& type);
bool GetCallTargetInfo(const Expr& callee,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       CallTargetInfo* out,
                       std::string* error);

enum class TypeUse : uint8_t {
  Value,
  Return,
};

const std::unordered_set<std::string> kPrimitiveTypes = {
  "i8", "i16", "i32", "i64", "i128",
  "u8", "u16", "u32", "u64", "u128",
  "f32", "f64",
  "bool", "char", "string",
};

bool IsReservedImportPath(const std::string& path) {
  static const std::unordered_set<std::string> kReserved = {
    "Math",
    "IO",
    "Time",
    "File",
    "Core.DL",
    "Core.Os",
    "Core.Fs",
    "Core.Log",
  };
  return kReserved.find(path) != kReserved.end();
}

bool IsPrimitiveCastName(const std::string& name) {
  if (name == "string") return false;
  return kPrimitiveTypes.find(name) != kPrimitiveTypes.end();
}

bool IsIoPrintName(const std::string& name);

bool IsIoPrintCallExpr(const Expr& callee) {
  return callee.kind == ExprKind::Member &&
         callee.op == "." &&
         !callee.children.empty() &&
         callee.children[0].kind == ExprKind::Identifier &&
         callee.children[0].text == "IO" &&
         IsIoPrintName(callee.text);
}

bool CountFormatPlaceholders(const std::string& fmt,
                             size_t* out_count,
                             std::string* error) {
  if (!out_count) return false;
  *out_count = 0;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{') {
      if (i + 1 >= fmt.size() || fmt[i + 1] != '}') {
        if (error) *error = "invalid format string: expected '{}' placeholder";
        return false;
      }
      ++(*out_count);
      ++i;
      continue;
    }
    if (fmt[i] == '}') {
      if (error) *error = "invalid format string: unmatched '}'";
      return false;
    }
  }
  return true;
}

bool IsReservedModuleEnabled(const ValidateContext& ctx, const std::string& name) {
  return ctx.reserved_imports.find(name) != ctx.reserved_imports.end() ||
         ctx.reserved_import_aliases.find(name) != ctx.reserved_import_aliases.end();
}

bool ResolveReservedModuleName(const ValidateContext& ctx,
                               const std::string& name,
                               std::string* out) {
  if (!out) return false;
  if (ctx.reserved_imports.find(name) != ctx.reserved_imports.end()) {
    *out = name;
    return true;
  }
  auto it = ctx.reserved_import_aliases.find(name);
  if (it != ctx.reserved_import_aliases.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool GetModuleNameFromExpr(const Expr& base, std::string* out) {
  if (!out) return false;
  if (base.kind == ExprKind::Identifier) {
    *out = base.text;
    return true;
  }
  if (base.kind == ExprKind::Member && base.op == "." && !base.children.empty()) {
    const Expr& root = base.children[0];
    if (root.kind == ExprKind::Identifier && root.text == "Core") {
      *out = "Core." + base.text;
      return true;
    }
  }
  return false;
}

std::string NormalizeCoreDlMember(const std::string& name) {
  if (name == "Open") return "open";
  if (name == "Sym") return "sym";
  if (name == "Close") return "close";
  if (name == "LastError") return "last_error";
  if (name == "CallI32") return "call_i32";
  if (name == "CallI64") return "call_i64";
  if (name == "CallF32") return "call_f32";
  if (name == "CallF64") return "call_f64";
  if (name == "CallStr0") return "call_str0";
  return name;
}

bool IsCoreDlOpenCallExpr(const Expr& expr, const ValidateContext& ctx) {
  if (expr.kind != ExprKind::Call || expr.children.empty()) return false;
  const Expr& callee = expr.children[0];
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  if (!IsReservedModuleEnabled(ctx, module_name)) return false;
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module_name, &resolved)) return false;
  return resolved == "Core.DL" && NormalizeCoreDlMember(callee.text) == "open";
}

bool GetDlOpenManifestModule(const Expr& expr,
                             const ValidateContext& ctx,
                             std::string* out_module) {
  if (!out_module) return false;
  if (!IsCoreDlOpenCallExpr(expr, ctx)) return false;
  if (expr.args.size() != 2) return false;
  if (expr.args[1].kind != ExprKind::Identifier) return false;
  const std::string& module = expr.args[1].text;
  auto mod_it = ctx.externs_by_module.find(module);
  if (mod_it == ctx.externs_by_module.end() || mod_it->second.empty()) return false;
  *out_module = module;
  return true;
}

bool IsSupportedDlAbiType(const TypeRef& type,
                          const ValidateContext& ctx,
                          bool allow_void) {
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (allow_void && type.name == "void") return true;
  if (type.name == "i8" || type.name == "i16" || type.name == "i32" || type.name == "i64" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64" ||
      type.name == "f32" || type.name == "f64" || type.name == "bool" || type.name == "char" ||
      type.name == "string") {
    return true;
  }
  if (ctx.enum_types.find(type.name) != ctx.enum_types.end()) return true;
  return ctx.artifacts.find(type.name) != ctx.artifacts.end();
}

bool IsSupportedDlDynamicSignature(const ExternDecl& ext,
                                   const ValidateContext& ctx,
                                   std::string* error) {
  if (!IsSupportedDlAbiType(ext.return_type, ctx, true)) {
    if (error) {
      *error = "dynamic DL return type for '" + ext.module + "." + ext.name +
               "' is not ABI-supported";
    }
    return false;
  }
  for (const auto& p : ext.params) {
    if (!IsSupportedDlAbiType(p.type, ctx, false)) {
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

TypeRef MakeSimpleType(const std::string& name) {
  TypeRef out;
  out.name = name;
  out.pointer_depth = 0;
  out.type_args.clear();
  out.dims.clear();
  out.is_proc = false;
  out.proc_params.clear();
  out.proc_return.reset();
  return out;
}

TypeRef MakeListType(const std::string& name) {
  TypeRef out = MakeSimpleType(name);
  TypeDim dim;
  dim.is_list = true;
  dim.has_size = false;
  dim.size = 0;
  out.dims.push_back(dim);
  return out;
}

bool GetReservedModuleVarType(const ValidateContext& ctx,
                              const std::string& module,
                              const std::string& member,
                              TypeRef* out) {
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module, &resolved)) return false;
  if (resolved == "Math" && member == "PI") {
    if (out) *out = MakeSimpleType("f64");
    return true;
  }
  return false;
}

bool GetReservedModuleCallTarget(const ValidateContext& ctx,
                                 const std::string& module,
                                 const std::string& member,
                                 CallTargetInfo* out) {
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module, &resolved)) return false;
  if (!out) return false;
  out->params.clear();
  out->type_params.clear();
  out->is_proc = false;
  if (resolved == "Math") {
    if (member == "abs") {
      out->params.push_back(MakeSimpleType("T"));
      out->return_type = MakeSimpleType("T");
      out->return_mutability = Mutability::Mutable;
      out->type_params = {"T"};
      return true;
    }
    if (member == "min" || member == "max") {
      out->params.push_back(MakeSimpleType("T"));
      out->params.push_back(MakeSimpleType("T"));
      out->return_type = MakeSimpleType("T");
      out->return_mutability = Mutability::Mutable;
      out->type_params = {"T"};
      return true;
    }
  }
  if (resolved == "Time") {
    if (member == "mono_ns" || member == "wall_ns") {
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Core.DL") {
    std::string dl_member = member;
    if (dl_member == "Open") dl_member = "open";
    else if (dl_member == "Sym") dl_member = "sym";
    else if (dl_member == "Close") dl_member = "close";
    else if (dl_member == "LastError") dl_member = "last_error";
    else if (dl_member == "CallI32") dl_member = "call_i32";
    else if (dl_member == "CallI64") dl_member = "call_i64";
    else if (dl_member == "CallF32") dl_member = "call_f32";
    else if (dl_member == "CallF64") dl_member = "call_f64";
    else if (dl_member == "CallStr0") dl_member = "call_str0";
    if (dl_member == "open") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "sym") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "close") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "last_error") {
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_i32") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_i64") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_f32") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("f32"));
      out->params.push_back(MakeSimpleType("f32"));
      out->return_type = MakeSimpleType("f32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_f64") {
      out->params.push_back(MakeSimpleType("i64"));
      out->params.push_back(MakeSimpleType("f64"));
      out->params.push_back(MakeSimpleType("f64"));
      out->return_type = MakeSimpleType("f64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (dl_member == "call_str0") {
      out->params.push_back(MakeSimpleType("i64"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Core.Os") {
    if (member == "args_count") {
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "args_get") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "env_get") {
      out->params.push_back(MakeSimpleType("string"));
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "cwd_get") {
      out->return_type = MakeSimpleType("string");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "time_mono_ns" || member == "time_wall_ns") {
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "sleep_ms") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Core.Fs") {
    if (member == "open") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "close") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "read" || member == "write") {
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Core.Log") {
    if (member == "log") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (module == "File") {
    if (member == "open") {
      out->params.push_back(MakeSimpleType("string"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "close") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("void");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "read" || member == "write") {
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  return false;
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
}

bool CloneTypeRef(const TypeRef& src, TypeRef* out) {
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
    TypeRef copy;
    if (!CloneTypeRef(arg, &copy)) return false;
    out->type_args.push_back(std::move(copy));
  }
  for (const auto& param : src.proc_params) {
    TypeRef copy;
    if (!CloneTypeRef(param, &copy)) return false;
    out->proc_params.push_back(std::move(copy));
  }
  if (src.proc_return) {
    TypeRef copy;
    if (!CloneTypeRef(*src.proc_return, &copy)) return false;
    out->proc_return = std::make_unique<TypeRef>(std::move(copy));
  }
  return true;
}

bool CloneTypeVector(const std::vector<TypeRef>& src, std::vector<TypeRef>* out) {
  if (!out) return false;
  out->clear();
  out->reserve(src.size());
  for (const auto& item : src) {
    TypeRef copy;
    if (!CloneTypeRef(item, &copy)) return false;
    out->push_back(std::move(copy));
  }
  return true;
}

bool TypeDimsEqual(const std::vector<TypeDim>& a, const std::vector<TypeDim>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].is_list != b[i].is_list) return false;
    if (a[i].has_size != b[i].has_size) return false;
    if (a[i].has_size && a[i].size != b[i].size) return false;
  }
  return true;
}

bool TypeArgsEqual(const std::vector<TypeRef>& a, const std::vector<TypeRef>& b);
bool IsIntegerTypeName(const std::string& name);

bool TypeEquals(const TypeRef& a, const TypeRef& b) {
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
  } else {
    if (a.name != b.name) return false;
    if (!TypeArgsEqual(a.type_args, b.type_args)) return false;
    if (!TypeDimsEqual(a.dims, b.dims)) return false;
  }
  return true;
}

bool IsIntegerLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Integer;
}

bool IsIntegerScalarTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128";
}

bool TypesCompatibleForExpr(const TypeRef& expected, const TypeRef& actual, const Expr& expr) {
  if (TypeEquals(expected, actual)) return true;
  if (IsIntegerLiteralExpr(expr) &&
      expected.pointer_depth == 0 && actual.pointer_depth == 0 &&
      expected.dims.empty() && actual.dims.empty() &&
      IsIntegerScalarTypeName(expected.name)) {
    return true;
  }
  return false;
}

bool TypeArgsEqual(const std::vector<TypeRef>& a, const std::vector<TypeRef>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!TypeEquals(a[i], b[i])) return false;
  }
  return true;
}

bool ApplyTypeSubstitution(TypeRef* type,
                           const std::unordered_map<std::string, TypeRef>& mapping) {
  if (!type) return false;
  for (auto& arg : type->type_args) {
    if (!ApplyTypeSubstitution(&arg, mapping)) return false;
  }
  if (type->is_proc) {
    for (auto& param : type->proc_params) {
      if (!ApplyTypeSubstitution(&param, mapping)) return false;
    }
    if (type->proc_return) {
      if (!ApplyTypeSubstitution(type->proc_return.get(), mapping)) return false;
    }
  }
  auto it = mapping.find(type->name);
  if (it == mapping.end()) return true;
  TypeRef replacement;
  if (!CloneTypeRef(it->second, &replacement)) return false;
  replacement.pointer_depth += type->pointer_depth;
  if (!type->dims.empty()) {
    replacement.dims.insert(replacement.dims.end(), type->dims.begin(), type->dims.end());
  }
  *type = std::move(replacement);
  return true;
}

bool SubstituteTypeParams(const TypeRef& src,
                          const std::unordered_map<std::string, TypeRef>& mapping,
                          TypeRef* out) {
  if (!out) return false;
  if (!CloneTypeRef(src, out)) return false;
  return ApplyTypeSubstitution(out, mapping);
}

bool BuildArtifactTypeParamMap(const TypeRef& instance_type,
                               const ArtifactDecl* artifact,
                               std::unordered_map<std::string, TypeRef>* out,
                               std::string* error) {
  if (!out) return false;
  out->clear();
  if (!artifact) return false;
  if (artifact->generics.empty()) return true;
  if (instance_type.type_args.size() != artifact->generics.size()) {
    if (error) {
      *error = "generic type argument count mismatch for " + artifact->name;
    }
    return false;
  }
  for (size_t i = 0; i < artifact->generics.size(); ++i) {
    TypeRef copy;
    if (!CloneTypeRef(instance_type.type_args[i], &copy)) return false;
    (*out)[artifact->generics[i]] = std::move(copy);
  }
  return true;
}

bool UnifyTypeParams(const TypeRef& param,
                     const TypeRef& arg,
                     const std::unordered_set<std::string>& type_params,
                     std::unordered_map<std::string, TypeRef>* mapping) {
  if (!mapping) return false;
  if (type_params.find(param.name) != type_params.end()) {
    if (!param.dims.empty()) {
      if (!TypeDimsEqual(param.dims, arg.dims)) return false;
      TypeRef base;
      if (!CloneTypeRef(arg, &base)) return false;
      base.dims.clear();
      auto it = mapping->find(param.name);
      if (it == mapping->end()) {
        (*mapping)[param.name] = std::move(base);
        return true;
      }
      return TypeEquals(it->second, base);
    }
    auto it = mapping->find(param.name);
    if (it == mapping->end()) {
      TypeRef copy;
      if (!CloneTypeRef(arg, &copy)) return false;
      (*mapping)[param.name] = std::move(copy);
      return true;
    }
    return TypeEquals(it->second, arg);
  }
  if (param.pointer_depth != arg.pointer_depth) return false;
  if (param.is_proc != arg.is_proc) return false;
  if (!TypeDimsEqual(param.dims, arg.dims)) return false;
  if (param.name != arg.name) return false;
  if (param.type_args.size() != arg.type_args.size()) return false;
  for (size_t i = 0; i < param.type_args.size(); ++i) {
    if (!UnifyTypeParams(param.type_args[i], arg.type_args[i], type_params, mapping)) return false;
  }
  if (param.is_proc) {
    if (param.proc_params.size() != arg.proc_params.size()) return false;
    for (size_t i = 0; i < param.proc_params.size(); ++i) {
      if (!UnifyTypeParams(param.proc_params[i], arg.proc_params[i], type_params, mapping)) return false;
    }
    if (param.proc_return && arg.proc_return) {
      if (!UnifyTypeParams(*param.proc_return, *arg.proc_return, type_params, mapping)) return false;
    } else if (param.proc_return || arg.proc_return) {
      return false;
    }
  }
  return true;
}

bool InferTypeArgsFromCall(const std::vector<TypeRef>& param_types,
                           const std::vector<Expr>& call_args,
                           const std::unordered_set<std::string>& type_params,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           std::unordered_map<std::string, TypeRef>* out_mapping) {
  if (!out_mapping) return false;
  out_mapping->clear();
  if (param_types.size() != call_args.size()) return false;
  for (size_t i = 0; i < param_types.size(); ++i) {
    TypeRef arg_type;
    if (!InferExprType(call_args[i], ctx, scopes, current_artifact, &arg_type)) return false;
    if (!UnifyTypeParams(param_types[i], arg_type, type_params, out_mapping)) return false;
  }
  for (const auto& name : type_params) {
    if (out_mapping->find(name) == out_mapping->end()) return false;
  }
  return true;
}

bool CheckTypeRef(const TypeRef& type,
                  const ValidateContext& ctx,
                  const std::unordered_set<std::string>& type_params,
                  TypeUse use,
                  std::string* error) {
  if (type.pointer_depth > 0) {
    TypeRef pointee;
    if (!CloneTypeRef(type, &pointee)) return false;
    pointee.pointer_depth -= 1;
    if (pointee.pointer_depth == 0 && pointee.name == "void") {
      if (!pointee.type_args.empty()) {
        if (error) *error = "void cannot have type arguments";
        PrefixErrorLocation(type.line, type.column, error);
        return false;
      }
      return true;
    }
    return CheckTypeRef(pointee, ctx, type_params, TypeUse::Value, error);
  }
  if (type.is_proc) {
    for (const auto& param : type.proc_params) {
      if (!CheckTypeRef(param, ctx, type_params, TypeUse::Value, error)) return false;
    }
    if (!type.proc_return) {
      if (error) *error = "procedure type missing return type";
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    return CheckTypeRef(*type.proc_return, ctx, type_params, TypeUse::Return, error);
  }

  if (type.name == "void") {
    if (use != TypeUse::Return) {
      if (error) *error = "void is only valid as a return type";
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (!type.type_args.empty()) {
      if (error) *error = "void cannot have type arguments";
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    return true;
  }

  const bool is_primitive = kPrimitiveTypes.find(type.name) != kPrimitiveTypes.end();
  const bool is_type_param = type_params.find(type.name) != type_params.end();
  const bool is_user_type = ctx.top_level.find(type.name) != ctx.top_level.end();

  if (IsReservedModuleEnabled(ctx, type.name)) {
    if (error) *error = "module is not a type: " + type.name;
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  if (!is_primitive && !is_type_param && !is_user_type) {
    if (error) *error = "unknown type: " + type.name;
    PrefixErrorLocation(type.line, type.column, error);
    return false;
  }

  if (is_user_type && !is_type_param) {
    if (ctx.modules.find(type.name) != ctx.modules.end()) {
      if (error) *error = "module is not a type: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (ctx.functions.find(type.name) != ctx.functions.end()) {
      if (error) *error = "function is not a type: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (ctx.enum_types.find(type.name) != ctx.enum_types.end()) {
      if (!type.type_args.empty()) {
        if (error) *error = "enum type cannot have type arguments: " + type.name;
        PrefixErrorLocation(type.line, type.column, error);
        return false;
      }
    }
    auto art_it = ctx.artifact_generics.find(type.name);
    if (art_it != ctx.artifact_generics.end()) {
      const size_t expected = art_it->second;
      if (type.type_args.size() != expected) {
        if (error) {
          *error = "generic type argument count mismatch for " + type.name;
        }
        PrefixErrorLocation(type.line, type.column, error);
        return false;
      }
    }
  }

  if (!type.type_args.empty()) {
    if (is_primitive) {
      if (error) *error = "primitive type cannot have type arguments: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    if (is_type_param) {
      if (error) *error = "type parameter cannot have type arguments: " + type.name;
      PrefixErrorLocation(type.line, type.column, error);
      return false;
    }
    for (const auto& arg : type.type_args) {
      if (!CheckTypeRef(arg, ctx, type_params, TypeUse::Value, error)) return false;
    }
  }

  return true;
}

const LocalInfo* FindLocal(const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const std::string& name);
const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name);
const VarDecl* FindArtifactField(const ArtifactDecl* artifact, const std::string& name);
const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name);
const FuncDecl* FindArtifactMethod(const ArtifactDecl* artifact, const std::string& name);
bool CheckCompoundAssignOp(const std::string& op,
                           const TypeRef& lhs,
                           const TypeRef& rhs,
                           std::string* error);
bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error);

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out) {
  if (!out) return false;
  switch (expr.kind) {
    case ExprKind::Literal:
      out->is_proc = false;
      out->type_args.clear();
      out->dims.clear();
      switch (expr.literal_kind) {
        case LiteralKind::Integer: out->name = "i32"; break;
        case LiteralKind::Float: out->name = "f64"; break;
        case LiteralKind::String: out->name = "string"; break;
        case LiteralKind::Char: out->name = "char"; break;
        case LiteralKind::Bool: out->name = "bool"; break;
      }
      return true;
    case ExprKind::Identifier: {
      if (expr.text == "self") return false;
      if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
        if (!local->type) return false;
        return CloneTypeRef(*local->type, out);
      }
      auto global_it = ctx.globals.find(expr.text);
      if (global_it != ctx.globals.end()) {
        return CloneTypeRef(global_it->second->type, out);
      }
      return false;
    }
    case ExprKind::Member: {
      if (expr.op != "." || expr.children.empty()) return false;
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier && base.text == "Core") {
        return true;
      }
      if (base.kind == ExprKind::Identifier) {
        if (base.text == "self") {
          const VarDecl* field = FindArtifactField(current_artifact, expr.text);
          if (field) return CloneTypeRef(field->type, out);
          const FuncDecl* method = FindArtifactMethod(current_artifact, expr.text);
          if (method) return CloneTypeRef(method->return_type, out);
          return false;
        }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        if (const VarDecl* var = FindModuleVar(module_it->second, expr.text)) {
          return CloneTypeRef(var->type, out);
        }
        if (const FuncDecl* fn = FindModuleFunc(module_it->second, expr.text)) {
          return CloneTypeRef(fn->return_type, out);
        }
        return false;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (GetReservedModuleVarType(ctx, module_name, expr.text, out)) {
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(expr.text);
          if (ext_it != ext_mod_it->second.end()) {
            return CloneTypeRef(ext_it->second->return_type, out);
          }
          return false;
        }
      }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          if (!local->type) return false;
          auto artifact_it = ctx.artifacts.find(local->type->name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, nullptr)) return false;
          }
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(field->type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(method->return_type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, nullptr)) return false;
          }
          if (const VarDecl* field = FindArtifactField(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(field->type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
          if (const FuncDecl* method = FindArtifactMethod(artifact, expr.text)) {
            TypeRef resolved;
            if (!SubstituteTypeParams(method->return_type, mapping, &resolved)) return false;
            return CloneTypeRef(resolved, out);
          }
        }
      }
      return false;
    }
    case ExprKind::Call: {
      if (expr.children.empty()) return false;
      const Expr& callee = expr.children[0];
      if (callee.kind == ExprKind::Identifier) {
        if (callee.text == "len") {
          out->name = "i32";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        if (callee.text == "str") {
          out->name = "string";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        if (IsPrimitiveCastName(callee.text)) {
          out->name = callee.text;
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
      }
      CallTargetInfo info;
      if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, nullptr)) return false;
      if (info.type_params.empty()) {
        return CloneTypeRef(info.return_type, out);
      }
      std::unordered_map<std::string, TypeRef> mapping;
      if (!expr.type_args.empty()) {
        if (expr.type_args.size() != info.type_params.size()) return false;
        for (size_t i = 0; i < info.type_params.size(); ++i) {
          TypeRef copy;
          if (!CloneTypeRef(expr.type_args[i], &copy)) return false;
          mapping[info.type_params[i]] = std::move(copy);
        }
      } else {
        std::unordered_set<std::string> type_param_set(info.type_params.begin(),
                                                       info.type_params.end());
        if (!InferTypeArgsFromCall(info.params, expr.args, type_param_set,
                                   ctx, scopes, current_artifact, &mapping)) {
          return false;
        }
      }
      TypeRef resolved;
      if (!SubstituteTypeParams(info.return_type, mapping, &resolved)) return false;
      return CloneTypeRef(resolved, out);
    }
    case ExprKind::Index: {
      TypeRef base_type;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &base_type)) return false;
      if (base_type.dims.empty()) return false;
      TypeRef result;
      if (!CloneTypeRef(base_type, &result)) return false;
      result.dims.erase(result.dims.begin());
      result.is_proc = false;
      result.proc_params.clear();
      result.proc_return.reset();
      return CloneTypeRef(result, out);
    }
    case ExprKind::Unary: {
      if (expr.children.empty()) return false;
      TypeRef operand;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) return false;
      if (!IsScalarType(operand)) return false;
      const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
      if (op == "!") {
        if (!IsBoolTypeName(operand.name)) return false;
        out->name = "bool";
        out->pointer_depth = 0;
        out->type_args.clear();
        out->dims.clear();
        out->is_proc = false;
        out->proc_params.clear();
        out->proc_return.reset();
        return true;
      }
      if (op == "++" || op == "--" || op == "-") {
        if (!IsNumericTypeName(operand.name)) return false;
        return CloneTypeRef(operand, out);
      }
      return false;
    }
    case ExprKind::Binary: {
      if (expr.children.size() < 2) return false;
      TypeRef lhs;
      TypeRef rhs;
      if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &lhs)) return false;
      if (!InferExprType(expr.children[1], ctx, scopes, current_artifact, &rhs)) return false;
      if (!IsScalarType(lhs) || !IsScalarType(rhs)) return false;

      TypeRef common;
      if (TypeEquals(lhs, rhs)) {
        if (!CloneTypeRef(lhs, &common)) return false;
      } else {
        const bool lhs_lit = IsIntegerLiteralExpr(expr.children[0]);
        const bool rhs_lit = IsIntegerLiteralExpr(expr.children[1]);
        const bool lhs_int = IsIntegerScalarTypeName(lhs.name);
        const bool rhs_int = IsIntegerScalarTypeName(rhs.name);
        if (lhs_lit && rhs_int) {
          if (!CloneTypeRef(rhs, &common)) return false;
        } else if (rhs_lit && lhs_int) {
          if (!CloneTypeRef(lhs, &common)) return false;
        } else {
          return false;
        }
      }

      const std::string& op = expr.op;
      if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
          op == "&&" || op == "||") {
        out->name = "bool";
        out->pointer_depth = 0;
        out->type_args.clear();
        out->dims.clear();
        out->is_proc = false;
        out->proc_params.clear();
        out->proc_return.reset();
        return true;
      }

      if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
          op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" ||
          op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
          op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        return CloneTypeRef(common, out);
      }
      return false;
    }
    default:
      return false;
  }
}

bool CollectTypeParams(const std::vector<std::string>& generics,
                        std::unordered_set<std::string>* out,
                        std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& name : generics) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  return true;
}

bool CollectTypeParamsMerged(const std::vector<std::string>& a,
                             const std::vector<std::string>& b,
                             std::unordered_set<std::string>* out,
                             std::string* error) {
  if (!out) return false;
  out->clear();
  for (const auto& name : a) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  for (const auto& name : b) {
    if (!out->insert(name).second) {
      if (error) *error = "duplicate generic parameter: " + name;
      return false;
    }
  }
  return true;
}

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               const std::unordered_set<std::string>& type_params,
               const TypeRef* expected_return,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error);

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error);

bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error);

bool CheckArrayLiteralElementTypes(const Expr& expr,
                                   const ValidateContext& ctx,
                                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                   const ArtifactDecl* current_artifact,
                                   const std::vector<TypeDim>& dims,
                                   size_t dim_index,
                                   const TypeRef& element_type,
                                   std::string* error);

bool CheckListLiteralElementTypes(const Expr& expr,
                                  const ValidateContext& ctx,
                                  const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                  const ArtifactDecl* current_artifact,
                                  const TypeRef& list_type,
                                  std::string* error);

bool CheckBoolCondition(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error);

bool StmtReturns(const Stmt& stmt);
bool StmtsReturn(const std::vector<Stmt>& stmts);

const LocalInfo* FindLocal(const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const std::string& name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) return &found->second;
  }
  return nullptr;
}

bool AddLocal(std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
              const std::string& name,
              const LocalInfo& info,
              std::string* error) {
  if (scopes.empty()) scopes.emplace_back();
  auto& current = scopes.back();
  if (!current.emplace(name, info).second) {
    if (error) *error = "duplicate local declaration: " + name;
    return false;
  }
  return true;
}

bool IsAssignOp(const std::string& op) {
  return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
         op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=";
}

const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& var : module->variables) {
    if (var.name == name) return &var;
  }
  return nullptr;
}

const VarDecl* FindArtifactField(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return nullptr;
  for (const auto& field : artifact->fields) {
    if (field.name == name) return &field;
  }
  return nullptr;
}

const FuncDecl* FindArtifactMethod(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return nullptr;
  for (const auto& method : artifact->methods) {
    if (method.name == name) return &method;
  }
  return nullptr;
}

bool IsArtifactMemberName(const ArtifactDecl* artifact, const std::string& name) {
  if (!artifact) return false;
  if (FindArtifactField(artifact, name)) return true;
  if (FindArtifactMethod(artifact, name)) return true;
  return false;
}

const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name) {
  if (!module) return nullptr;
  for (const auto& fn : module->functions) {
    if (fn.name == name) return &fn;
  }
  return nullptr;
}

bool CheckCallArgs(const FuncDecl* fn, size_t arg_count, std::string* error) {
  if (!fn) return false;
  if (fn->params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch for " + fn->name + ": expected " +
               std::to_string(fn->params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckProcTypeArgs(const TypeRef* type, size_t arg_count, std::string* error) {
  if (!type || !type->is_proc) return false;
  if (type->proc_params.size() != arg_count) {
    if (error) {
      *error = "call argument count mismatch: expected " +
               std::to_string(type->proc_params.size()) + ", got " + std::to_string(arg_count);
    }
    return false;
  }
  return true;
}

bool CheckCallTarget(const Expr& callee,
                     size_t arg_count,
                     const ValidateContext& ctx,
                     const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                     const ArtifactDecl* current_artifact,
                     std::string* error) {
  if (callee.kind == ExprKind::FnLiteral) {
    if (callee.fn_params.size() != arg_count) {
      if (error) {
        *error = "call argument count mismatch for fn literal: expected " +
                 std::to_string(callee.fn_params.size()) + ", got " + std::to_string(arg_count);
      }
      return false;
    }
    return true;
  }
  if (callee.kind == ExprKind::Identifier) {
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      return CheckCallArgs(fn_it->second, arg_count, error);
    }
    auto ext_it = ctx.externs.find(callee.text);
    if (ext_it != ctx.externs.end()) {
      if (ext_it->second->params.size() != arg_count) {
        if (error) {
          *error = "call argument count mismatch for extern " + callee.text +
                   ": expected " + std::to_string(ext_it->second->params.size()) +
                   ", got " + std::to_string(arg_count);
        }
        return false;
      }
      return true;
    }
    if (const LocalInfo* local = FindLocal(scopes, callee.text)) {
      if (local->type && local->type->is_proc) {
        return CheckProcTypeArgs(local->type, arg_count, error);
      }
      if (error) *error = "attempt to call non-function: " + callee.text;
      return false;
    }
    auto global_it = ctx.globals.find(callee.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->type.is_proc) {
        return CheckProcTypeArgs(&global_it->second->type, arg_count, error);
      }
      if (error) *error = "attempt to call non-function: " + callee.text;
      return false;
    }
    return true;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "IO" && IsIoPrintName(callee.text)) {
        if (arg_count == 0) {
          if (error) *error = "call argument count mismatch for IO." + callee.text;
          return false;
        }
        return true;
      }
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (FindArtifactField(current_artifact, callee.text)) {
          if (error) *error = "attempt to call non-function: self." + callee.text;
          return false;
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->dl_module.empty()) {
          auto mod_it = ctx.externs_by_module.find(local->dl_module);
          if (mod_it != ctx.externs_by_module.end()) {
            auto ext_it = mod_it->second.find(callee.text);
            if (ext_it != mod_it->second.end()) {
              if (!IsSupportedDlDynamicSignature(*ext_it->second, ctx, error)) return false;
              if (ext_it->second->params.size() != arg_count) {
                if (error) {
                  *error = "call argument count mismatch for dynamic symbol " +
                           base.text + "." + callee.text + ": expected " +
                           std::to_string(ext_it->second->params.size()) +
                           ", got " + std::to_string(arg_count);
                }
                return false;
              }
              return true;
            }
            if (error) {
              *error = "unknown dynamic symbol: " + base.text + "." + callee.text;
            }
            return false;
          }
        }
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) return CheckCallArgs(fn, arg_count, error);
        if (FindModuleVar(module_it->second, callee.text)) {
          const VarDecl* var = FindModuleVar(module_it->second, callee.text);
          if (var && var->type.is_proc) {
            return CheckProcTypeArgs(&var->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
        return true;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          CallTargetInfo info;
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, &info)) {
            std::string resolved_module;
            const bool is_core_dl_open =
                ResolveReservedModuleName(ctx, module_name, &resolved_module) &&
                resolved_module == "Core.DL" &&
                NormalizeCoreDlMember(callee.text) == "open";
            if (!is_core_dl_open && info.params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected " + std::to_string(info.params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            if (is_core_dl_open && arg_count != 1 && arg_count != 2) {
              if (error) {
                *error = "call argument count mismatch for " + module_name + "." + callee.text +
                         ": expected 1 or 2, got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            if (ext_it->second->params.size() != arg_count) {
              if (error) {
                *error = "call argument count mismatch for extern " + module_name + "." + callee.text +
                         ": expected " + std::to_string(ext_it->second->params.size()) +
                         ", got " + std::to_string(arg_count);
              }
              return false;
            }
            return true;
          }
          if (error) *error = "unknown extern member: " + module_name + "." + callee.text;
          return false;
        }
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (const VarDecl* field = FindArtifactField(artifact, callee.text)) {
          if (field->type.is_proc) {
            return CheckProcTypeArgs(&field->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
        return true;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) return CheckCallArgs(method, arg_count, error);
        if (const VarDecl* field = FindArtifactField(artifact, callee.text)) {
          if (field->type.is_proc) {
            return CheckProcTypeArgs(&field->type, arg_count, error);
          }
          if (error) *error = "attempt to call non-function: " + base.text + "." + callee.text;
          return false;
        }
      }
    }
  }
  return true;
}

bool GetCallTargetInfo(const Expr& callee,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       CallTargetInfo* out,
                       std::string* error) {
  if (!out) return false;
  if (callee.kind == ExprKind::FnLiteral) {
    out->params.clear();
    out->return_type = TypeRef{};
    out->return_mutability = Mutability::Mutable;
    out->type_params.clear();
    out->is_proc = true;
    for (const auto& param : callee.fn_params) {
      TypeRef copy;
      if (!CloneTypeRef(param.type, &copy)) return false;
      out->params.push_back(std::move(copy));
    }
    return true;
  }
  if (callee.kind == ExprKind::Identifier) {
    auto fn_it = ctx.functions.find(callee.text);
    if (fn_it != ctx.functions.end()) {
      out->params.clear();
      if (!CloneTypeRef(fn_it->second->return_type, &out->return_type)) return false;
      out->return_mutability = fn_it->second->return_mutability;
      out->type_params = fn_it->second->generics;
      out->is_proc = false;
      for (const auto& param : fn_it->second->params) {
        TypeRef copy;
        if (!CloneTypeRef(param.type, &copy)) return false;
        out->params.push_back(std::move(copy));
      }
      return true;
    }
    auto ext_it = ctx.externs.find(callee.text);
    if (ext_it != ctx.externs.end()) {
      out->params.clear();
      if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
      out->return_mutability = ext_it->second->return_mutability;
      out->type_params.clear();
      out->is_proc = false;
      for (const auto& param : ext_it->second->params) {
        TypeRef copy;
        if (!CloneTypeRef(param.type, &copy)) return false;
        out->params.push_back(std::move(copy));
      }
      return true;
    }
    if (const LocalInfo* local = FindLocal(scopes, callee.text)) {
      if (local->type && local->type->is_proc) {
        if (!CloneTypeVector(local->type->proc_params, &out->params)) return false;
        if (local->type->proc_return) {
          if (!CloneTypeRef(*local->type->proc_return, &out->return_type)) return false;
        }
        out->return_mutability = local->type->proc_return_mutability;
        out->type_params.clear();
        out->is_proc = true;
        return true;
      }
      return false;
    }
    auto global_it = ctx.globals.find(callee.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->type.is_proc) {
        if (!CloneTypeVector(global_it->second->type.proc_params, &out->params)) return false;
        if (global_it->second->type.proc_return) {
          if (!CloneTypeRef(*global_it->second->type.proc_return, &out->return_type)) return false;
        }
        out->return_mutability = global_it->second->type.proc_return_mutability;
        out->type_params.clear();
        out->is_proc = true;
        return true;
      }
      return false;
    }
    return false;
  }
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "IO" && IsIoPrintName(callee.text)) {
        out->params.clear();
        TypeRef param;
        param.name = "T";
        param.type_args.clear();
        param.dims.clear();
        param.is_proc = false;
        param.proc_params.clear();
        param.proc_return.reset();
        out->params.push_back(std::move(param));
        out->return_type = TypeRef{};
        out->return_type.name = "void";
        out->return_type.type_args.clear();
        out->return_type.dims.clear();
        out->return_type.is_proc = false;
        out->return_type.proc_params.clear();
        out->return_type.proc_return.reset();
        out->return_mutability = Mutability::Mutable;
        out->type_params = {"T"};
        out->is_proc = false;
        return true;
      }
      if (base.text == "self") {
        const FuncDecl* method = FindArtifactMethod(current_artifact, callee.text);
        if (!method) return false;
        out->params.clear();
        if (!CloneTypeRef(method->return_type, &out->return_type)) return false;
        out->return_mutability = method->return_mutability;
        out->type_params = method->generics;
        out->is_proc = false;
        for (const auto& param : method->params) {
          TypeRef copy;
          if (!CloneTypeRef(param.type, &copy)) return false;
          out->params.push_back(std::move(copy));
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->dl_module.empty()) {
          auto mod_it = ctx.externs_by_module.find(local->dl_module);
          if (mod_it != ctx.externs_by_module.end()) {
            auto ext_it = mod_it->second.find(callee.text);
            if (ext_it != mod_it->second.end()) {
              if (!IsSupportedDlDynamicSignature(*ext_it->second, ctx, error)) return false;
              out->params.clear();
              if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
              out->return_mutability = ext_it->second->return_mutability;
              out->type_params.clear();
              out->is_proc = false;
              for (const auto& param : ext_it->second->params) {
                TypeRef copy;
                if (!CloneTypeRef(param.type, &copy)) return false;
                out->params.push_back(std::move(copy));
              }
              return true;
            }
          }
        }
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const FuncDecl* fn = FindModuleFunc(module_it->second, callee.text);
        if (fn) {
          out->params.clear();
          if (!CloneTypeRef(fn->return_type, &out->return_type)) return false;
          out->return_mutability = fn->return_mutability;
          out->type_params = fn->generics;
          out->is_proc = false;
          for (const auto& param : fn->params) {
            TypeRef copy;
            if (!CloneTypeRef(param.type, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* var = FindModuleVar(module_it->second, callee.text);
        if (var && var->type.is_proc) {
          if (!CloneTypeVector(var->type.proc_params, &out->params)) return false;
          if (var->type.proc_return) {
            if (!CloneTypeRef(*var->type.proc_return, &out->return_type)) return false;
          }
          out->return_mutability = var->type.proc_return_mutability;
          out->type_params.clear();
          out->is_proc = true;
          return true;
        }
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name)) {
        if (IsReservedModuleEnabled(ctx, module_name)) {
          if (GetReservedModuleCallTarget(ctx, module_name, callee.text, out)) {
            return true;
          }
        }
        auto ext_mod_it = ctx.externs_by_module.find(module_name);
        if (ext_mod_it != ctx.externs_by_module.end()) {
          auto ext_it = ext_mod_it->second.find(callee.text);
          if (ext_it != ext_mod_it->second.end()) {
            out->params.clear();
            if (!CloneTypeRef(ext_it->second->return_type, &out->return_type)) return false;
            out->return_mutability = ext_it->second->return_mutability;
            out->type_params.clear();
            out->is_proc = false;
            for (const auto& param : ext_it->second->params) {
              TypeRef copy;
              if (!CloneTypeRef(param.type, &copy)) return false;
              out->params.push_back(std::move(copy));
            }
            return true;
          }
        }
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return false;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, error)) return false;
          }
          out->params.clear();
          TypeRef resolved_return;
          if (!SubstituteTypeParams(method->return_type, mapping, &resolved_return)) return false;
          if (!CloneTypeRef(resolved_return, &out->return_type)) return false;
          out->return_mutability = method->return_mutability;
          out->type_params = method->generics;
          out->is_proc = false;
          for (const auto& param : method->params) {
            TypeRef copy;
            if (!SubstituteTypeParams(param.type, mapping, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* field = FindArtifactField(artifact, callee.text);
        if (field && field->type.is_proc) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(*local->type, artifact, &mapping, error)) return false;
          }
          TypeRef resolved_field;
          if (!SubstituteTypeParams(field->type, mapping, &resolved_field)) return false;
          out->params.clear();
          out->type_params.clear();
          out->is_proc = true;
          out->return_mutability = resolved_field.proc_return_mutability;
          if (!CloneTypeVector(resolved_field.proc_params, &out->params)) return false;
          if (resolved_field.proc_return) {
            if (!CloneTypeRef(*resolved_field.proc_return, &out->return_type)) return false;
          }
          return true;
        }
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const FuncDecl* method = FindArtifactMethod(artifact, callee.text);
        if (method) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, error)) return false;
          }
          out->params.clear();
          TypeRef resolved_return;
          if (!SubstituteTypeParams(method->return_type, mapping, &resolved_return)) return false;
          if (!CloneTypeRef(resolved_return, &out->return_type)) return false;
          out->return_mutability = method->return_mutability;
          out->type_params = method->generics;
          out->is_proc = false;
          for (const auto& param : method->params) {
            TypeRef copy;
            if (!SubstituteTypeParams(param.type, mapping, &copy)) return false;
            out->params.push_back(std::move(copy));
          }
          return true;
        }
        const VarDecl* field = FindArtifactField(artifact, callee.text);
        if (field && field->type.is_proc) {
          std::unordered_map<std::string, TypeRef> mapping;
          if (artifact && !artifact->generics.empty()) {
            if (!BuildArtifactTypeParamMap(global_it->second->type, artifact, &mapping, error)) return false;
          }
          TypeRef resolved_field;
          if (!SubstituteTypeParams(field->type, mapping, &resolved_field)) return false;
          out->params.clear();
          out->type_params.clear();
          out->is_proc = true;
          out->return_mutability = resolved_field.proc_return_mutability;
          if (!CloneTypeVector(resolved_field.proc_params, &out->params)) return false;
          if (resolved_field.proc_return) {
            if (!CloneTypeRef(*resolved_field.proc_return, &out->return_type)) return false;
          }
          return true;
        }
      }
    }
  }
  if (error) *error = "attempt to call non-function";
  return false;
}

bool CheckCallArgTypes(const Expr& call_expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  if (call_expr.kind != ExprKind::Call || call_expr.children.empty()) return true;
  const Expr& callee = call_expr.children[0];
  if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
    const Expr& base = callee.children[0];
    std::string module_name;
    if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
      const std::string& mod = module_name;
      const std::string& name = callee.text;
      auto infer_arg = [&](size_t index, TypeRef* out_type) -> bool {
        if (!out_type) return false;
        if (index >= call_expr.args.size()) return false;
        return InferExprType(call_expr.args[index], ctx, scopes, current_artifact, out_type);
      };
      auto is_i32_buffer = [&](const TypeRef& t) -> bool {
        return t.name == "i32" && !t.is_proc && t.type_args.empty() &&
               t.dims.size() == 1;
      };
      if (mod == "Math") {
        if (name == "abs") {
          if (call_expr.args.size() != 1) return true;
          TypeRef arg;
          if (!infer_arg(0, &arg)) return true;
          if ((arg.name != "i32" && arg.name != "i64") || !arg.dims.empty() || arg.is_proc) {
            if (error) *error = "Math.abs expects i32 or i64 argument";
            return false;
          }
          return true;
        }
        if (name == "min" || name == "max") {
          if (call_expr.args.size() != 2) return true;
          TypeRef a;
          TypeRef b;
          if (!infer_arg(0, &a) || !infer_arg(1, &b)) return true;
          auto allowed = [&](const TypeRef& t) {
            return t.name == "i32" || t.name == "i64" || t.name == "f32" || t.name == "f64";
          };
          if (!allowed(a) || !allowed(b) || !TypeEquals(a, b) || !a.dims.empty() || !b.dims.empty()) {
            if (error) *error = "Math." + name + " expects two numeric arguments of the same type";
            return false;
          }
          return true;
        }
      }
      if (mod == "Time") {
        if (name == "mono_ns" || name == "wall_ns") {
          if (!call_expr.args.empty()) {
            if (error) *error = "Time." + name + " expects no arguments";
            return false;
          }
          return true;
        }
      }
      if (mod == "Core.DL" && NormalizeCoreDlMember(name) == "open") {
        if (call_expr.args.size() != 1 && call_expr.args.size() != 2) {
          if (error) *error = "Core.DL.open expects (string) or (string, manifest)";
          return false;
        }
        TypeRef path;
        if (!infer_arg(0, &path)) return true;
        if (path.name != "string" || !path.dims.empty()) {
          if (error) *error = "Core.DL.open expects first argument string path";
          return false;
        }
        if (call_expr.args.size() == 2) {
          if (call_expr.args[1].kind != ExprKind::Identifier) {
            if (error) *error = "Core.DL.open manifest must be an extern module identifier";
            return false;
          }
          const std::string manifest = call_expr.args[1].text;
          auto mod_it = ctx.externs_by_module.find(manifest);
          if (mod_it == ctx.externs_by_module.end() || mod_it->second.empty()) {
            if (error) *error = "Core.DL.open manifest has no extern symbols: " + manifest;
            return false;
          }
          for (const auto& entry : mod_it->second) {
            if (!IsSupportedDlDynamicSignature(*entry.second, ctx, error)) return false;
          }
        }
        return true;
      }
      if (mod == "File") {
        if (name == "open") {
          if (call_expr.args.size() != 2) return true;
          TypeRef path;
          TypeRef flags;
          if (!infer_arg(0, &path) || !infer_arg(1, &flags)) return true;
          if (path.name != "string" || !path.dims.empty() || flags.name != "i32" || !flags.dims.empty()) {
            if (error) *error = "File.open expects (string, i32)";
            return false;
          }
          return true;
        }
        if (name == "close") {
          if (call_expr.args.size() != 1) return true;
          TypeRef fd;
          if (!infer_arg(0, &fd)) return true;
          if (fd.name != "i32" || !fd.dims.empty()) {
            if (error) *error = "File.close expects (i32)";
            return false;
          }
          return true;
        }
        if (name == "read" || name == "write") {
          if (call_expr.args.size() != 3) return true;
          TypeRef fd;
          TypeRef buf;
          TypeRef len;
          if (!infer_arg(0, &fd) || !infer_arg(1, &buf) || !infer_arg(2, &len)) return true;
          if (fd.name != "i32" || !fd.dims.empty() || len.name != "i32" || !len.dims.empty() ||
              !is_i32_buffer(buf)) {
            if (error) *error = "File." + name + " expects (i32, i32[], i32)";
            return false;
          }
          return true;
        }
      }
    }
  }
  CallTargetInfo info;
  if (!GetCallTargetInfo(callee, ctx, scopes, current_artifact, &info, error)) return true;
  if (!info.type_params.empty() && !call_expr.type_args.empty()) {
    if (call_expr.type_args.size() != info.type_params.size()) {
      if (error) {
        *error = "generic type argument count mismatch: expected " +
                 std::to_string(info.type_params.size()) + ", got " +
                 std::to_string(call_expr.type_args.size());
      }
      return false;
    }
  } else if (info.type_params.empty() && !call_expr.type_args.empty()) {
    if (error) *error = "non-generic call cannot take type arguments";
    return false;
  }

  std::unordered_map<std::string, TypeRef> mapping;
  if (!info.type_params.empty()) {
    std::unordered_set<std::string> type_param_set(info.type_params.begin(), info.type_params.end());
    if (!call_expr.type_args.empty()) {
      for (size_t i = 0; i < info.type_params.size(); ++i) {
        TypeRef copy;
        if (!CloneTypeRef(call_expr.type_args[i], &copy)) return false;
        mapping[info.type_params[i]] = std::move(copy);
      }
    } else {
      if (!InferTypeArgsFromCall(info.params, call_expr.args, type_param_set,
                                 ctx, scopes, current_artifact, &mapping)) {
        if (error) *error = "cannot infer type arguments for call";
        return false;
      }
    }
  }

  for (size_t i = 0; i < info.params.size() && i < call_expr.args.size(); ++i) {
    TypeRef expected;
    if (!SubstituteTypeParams(info.params[i], mapping, &expected)) return false;
    TypeRef actual;
    if (!InferExprType(call_expr.args[i], ctx, scopes, current_artifact, &actual)) continue;
    if (!TypesCompatibleForExpr(expected, actual, call_expr.args[i])) {
      if (error) *error = "call argument type mismatch";
      return false;
    }
  }
  return true;
}

bool CheckAssignmentTarget(const Expr& target,
                           const ValidateContext& ctx,
                           const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                           const ArtifactDecl* current_artifact,
                           std::string* error) {
  std::function<bool(const Expr&)> is_mutable_expr = [&](const Expr& expr) -> bool {
    if (expr.kind == ExprKind::Identifier) {
      if (const LocalInfo* local = FindLocal(scopes, expr.text)) {
        return local->mutability == Mutability::Mutable;
      }
      auto global_it = ctx.globals.find(expr.text);
      if (global_it != ctx.globals.end()) {
        return global_it->second->mutability == Mutability::Mutable;
      }
      return true;
    }
    if (expr.kind == ExprKind::Member && expr.op == "." && !expr.children.empty()) {
      const Expr& base = expr.children[0];
      if (base.kind == ExprKind::Identifier) {
        if (base.text == "self") {
          const VarDecl* field = FindArtifactField(current_artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
          return true;
        }
        auto module_it = ctx.modules.find(base.text);
        if (module_it != ctx.modules.end()) {
          const VarDecl* var = FindModuleVar(module_it->second, expr.text);
          if (var) return var->mutability == Mutability::Mutable;
          return true;
        }
        if (const LocalInfo* local = FindLocal(scopes, base.text)) {
          auto artifact_it = ctx.artifacts.find(local->type ? local->type->name : "");
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          const VarDecl* field = FindArtifactField(artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
          return true;
        }
        auto global_it = ctx.globals.find(base.text);
        if (global_it != ctx.globals.end()) {
          auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
          const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
          const VarDecl* field = FindArtifactField(artifact, expr.text);
          if (field) return field->mutability == Mutability::Mutable;
        }
      }
      return true;
    }
    if (expr.kind == ExprKind::Call) {
      CallTargetInfo info;
      if (!GetCallTargetInfo(expr.children[0], ctx, scopes, current_artifact, &info, nullptr)) return true;
      return info.return_mutability == Mutability::Mutable;
    }
    if (expr.kind == ExprKind::Index) {
      if (expr.children.empty()) return true;
      return is_mutable_expr(expr.children[0]);
    }
    return true;
  };
  if (target.kind == ExprKind::Identifier) {
    if (target.text == "self") {
      if (error) *error = "cannot assign to self";
      return false;
    }
    if (const LocalInfo* local = FindLocal(scopes, target.text)) {
      if (local->mutability == Mutability::Immutable) {
        if (error) *error = "cannot assign to immutable local: " + target.text;
        return false;
      }
      return true;
    }
    auto global_it = ctx.globals.find(target.text);
    if (global_it != ctx.globals.end()) {
      if (global_it->second->mutability == Mutability::Immutable) {
        if (error) *error = "cannot assign to immutable variable: " + target.text;
        return false;
      }
      return true;
    }
    if (ctx.functions.find(target.text) != ctx.functions.end()) {
      if (error) *error = "cannot assign to function: " + target.text;
      return false;
    }
    return true;
  }
  if (target.kind == ExprKind::Member && target.op == "." && !target.children.empty()) {
    const Expr& base = target.children[0];
    if (!is_mutable_expr(base)) {
      if (error) *error = "cannot assign through immutable value";
      return false;
    }
    if (base.kind == ExprKind::Identifier) {
      if (base.text == "self") {
        const VarDecl* field = FindArtifactField(current_artifact, target.text);
        if (!field && FindArtifactMethod(current_artifact, target.text)) {
          if (error) *error = "cannot assign to method: self." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: self." + target.text;
          return false;
        }
        return true;
      }
      if (const LocalInfo* local = FindLocal(scopes, base.text)) {
        if (!local->type) return true;
        auto artifact_it = ctx.artifacts.find(local->type->name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, target.text);
        if (!field && FindArtifactMethod(artifact, target.text)) {
          if (error) *error = "cannot assign to method: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      auto module_it = ctx.modules.find(base.text);
      if (module_it != ctx.modules.end()) {
        const VarDecl* field = FindModuleVar(module_it->second, target.text);
        if (!field && FindModuleFunc(module_it->second, target.text)) {
          if (error) *error = "cannot assign to function: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable module member: " + base.text + "." + target.text;
          return false;
        }
        return true;
      }
      std::string module_name;
      if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
        if (error) *error = "cannot assign to immutable module member: " + module_name + "." + target.text;
        return false;
      }
      auto global_it = ctx.globals.find(base.text);
      if (global_it != ctx.globals.end()) {
        auto artifact_it = ctx.artifacts.find(global_it->second->type.name);
        const ArtifactDecl* artifact = artifact_it == ctx.artifacts.end() ? nullptr : artifact_it->second;
        const VarDecl* field = FindArtifactField(artifact, target.text);
        if (!field && FindArtifactMethod(artifact, target.text)) {
          if (error) *error = "cannot assign to method: " + base.text + "." + target.text;
          return false;
        }
        if (field && field->mutability == Mutability::Immutable) {
          if (error) *error = "cannot assign to immutable field: " + base.text + "." + target.text;
          return false;
        }
      }
    }
    return true;
  }
  if (target.kind == ExprKind::Index) {
    if (!target.children.empty() && !is_mutable_expr(target.children[0])) {
      if (error) *error = "cannot assign through immutable value";
      return false;
    }
    return true;
  }
  if (error) *error = "invalid assignment target";
  return false;
}

bool ValidateArtifactLiteral(const Expr& expr,
                             const ArtifactDecl* artifact,
                             const std::unordered_map<std::string, TypeRef>& type_mapping,
                             const ValidateContext& ctx,
                             const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                             const ArtifactDecl* current_artifact,
                             std::string* error) {
  if (!artifact) return true;
  const size_t field_count = artifact->fields.size();
  if (expr.children.size() > field_count) {
    if (error) *error = "too many positional values in artifact literal";
    return false;
  }
  std::unordered_set<std::string> seen;
  for (const auto& name : expr.field_names) {
    if (!seen.insert(name).second) {
      if (error) *error = "duplicate named field in artifact literal: " + name;
      return false;
    }
  }
  for (size_t i = 0; i < expr.children.size(); ++i) {
    if (i >= field_count) break;
    const auto& field = artifact->fields[i];
    if (seen.find(field.name) != seen.end()) {
      if (error) *error = "field specified twice in artifact literal: " + field.name;
      return false;
    }
    seen.insert(field.name);
    TypeRef value_type;
    if (InferExprType(expr.children[i], ctx, scopes, current_artifact, &value_type)) {
      TypeRef expected;
      if (!SubstituteTypeParams(field.type, type_mapping, &expected)) return false;
      if (!TypesCompatibleForExpr(expected, value_type, expr.children[i])) {
        if (error) *error = "artifact field type mismatch: " + field.name;
        return false;
      }
    }
  }
  if (!expr.field_names.empty()) {
    std::unordered_set<std::string> valid;
    std::unordered_map<std::string, const VarDecl*> field_map;
    for (const auto& field : artifact->fields) {
      valid.insert(field.name);
      field_map[field.name] = &field;
    }
    for (const auto& name : expr.field_names) {
      if (valid.find(name) == valid.end()) {
        if (error) *error = "unknown artifact field: " + name;
        return false;
      }
    }
    for (size_t i = 0; i < expr.field_names.size(); ++i) {
      const auto& name = expr.field_names[i];
      auto it = field_map.find(name);
      if (it == field_map.end()) continue;
      TypeRef value_type;
      if (InferExprType(expr.field_values[i], ctx, scopes, current_artifact, &value_type)) {
        TypeRef expected;
        if (!SubstituteTypeParams(it->second->type, type_mapping, &expected)) return false;
        if (!TypesCompatibleForExpr(expected, value_type, expr.field_values[i])) {
          if (error) *error = "artifact field type mismatch: " + name;
          return false;
        }
      }
    }
  }
  return true;
}

bool CheckStmt(const Stmt& stmt,
               const ValidateContext& ctx,
               const std::unordered_set<std::string>& type_params,
               const TypeRef* expected_return,
               bool return_is_void,
               int loop_depth,
               std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error) {
  switch (stmt.kind) {
    case StmtKind::Return:
      if (return_is_void && stmt.has_return_expr) {
        if (error) *error = "void function cannot return a value";
        return false;
      }
      if (!return_is_void && !stmt.has_return_expr) {
        if (error) *error = "non-void function must return a value";
        return false;
      }
      if (stmt.has_return_expr) {
        if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
        if (expected_return) {
          TypeRef actual;
          if (InferExprType(stmt.expr, ctx, scopes, current_artifact, &actual)) {
            if (!TypesCompatibleForExpr(*expected_return, actual, stmt.expr)) {
              if (error) *error = "return type mismatch";
              return false;
            }
          }
        }
        return true;
      }
      return true;
    case StmtKind::Expr:
      return CheckExpr(stmt.expr, ctx, scopes, current_artifact, error);
    case StmtKind::Assign:
      if (!CheckExpr(stmt.target, ctx, scopes, current_artifact, error)) return false;
      if (!CheckAssignmentTarget(stmt.target, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.expr, ctx, scopes, current_artifact, error)) return false;
      {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(stmt.target, ctx, scopes, current_artifact, &target_type);
        bool have_value = InferExprType(stmt.expr, ctx, scopes, current_artifact, &value_type);
        if (have_target && stmt.expr.kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(stmt.expr, target_type, error)) return false;
        }
        if (have_target && have_value && !TypesCompatibleForExpr(target_type, value_type, stmt.expr)) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        if (have_target && have_value && stmt.assign_op != "=") {
          std::string op = stmt.assign_op;
          if (!op.empty() && op.back() == '=') op.pop_back();
          if (!CheckCompoundAssignOp(op, target_type, value_type, error)) return false;
        }
        if (have_target &&
            (stmt.expr.kind == ExprKind::ArrayLiteral || stmt.expr.kind == ExprKind::ListLiteral) &&
            !target_type.dims.empty()) {
          if (!CheckArrayLiteralShape(stmt.expr, target_type.dims, 0, error)) return false;
          TypeRef base_type;
          if (!CloneTypeRef(target_type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(stmt.expr,
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             target_type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(stmt.expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   (stmt.expr.kind == ExprKind::ArrayLiteral || stmt.expr.kind == ExprKind::ListLiteral)) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
      }
      return true;
    case StmtKind::VarDecl:
      if (!CheckTypeRef(stmt.var_decl.type, ctx, type_params, TypeUse::Value, error)) return false;
      {
        LocalInfo info;
        info.mutability = stmt.var_decl.mutability;
        info.type = &stmt.var_decl.type;
        if (!AddLocal(scopes, stmt.var_decl.name, info, error)) return false;
      }
      if (stmt.var_decl.has_init_expr) {
        if (!CheckExpr(stmt.var_decl.init_expr, ctx, scopes, current_artifact, error)) return false;
        if (stmt.var_decl.init_expr.kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(stmt.var_decl.init_expr, stmt.var_decl.type, error)) {
            return false;
          }
        }
        if ((stmt.var_decl.init_expr.kind == ExprKind::ArrayLiteral ||
             stmt.var_decl.init_expr.kind == ExprKind::ListLiteral) &&
            !stmt.var_decl.type.dims.empty()) {
          if (!CheckArrayLiteralShape(stmt.var_decl.init_expr, stmt.var_decl.type.dims, 0, error)) {
            return false;
          }
          TypeRef base_type;
          if (!CloneTypeRef(stmt.var_decl.type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(stmt.var_decl.init_expr,
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             stmt.var_decl.type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(stmt.var_decl.init_expr,
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            stmt.var_decl.type,
                                            error)) {
            return false;
          }
        } else if (stmt.var_decl.init_expr.kind == ExprKind::ArrayLiteral ||
                   stmt.var_decl.init_expr.kind == ExprKind::ListLiteral) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
        TypeRef init_type;
        if (InferExprType(stmt.var_decl.init_expr, ctx, scopes, current_artifact, &init_type)) {
          if (!TypesCompatibleForExpr(stmt.var_decl.type, init_type, stmt.var_decl.init_expr)) {
            if (error) *error = "initializer type mismatch";
            return false;
          }
        }
        if (stmt.var_decl.init_expr.kind == ExprKind::ArtifactLiteral) {
          auto artifact_it = ctx.artifacts.find(stmt.var_decl.type.name);
          if (artifact_it != ctx.artifacts.end()) {
            std::unordered_map<std::string, TypeRef> mapping;
            if (!BuildArtifactTypeParamMap(stmt.var_decl.type,
                                           artifact_it->second,
                                           &mapping,
                                           error)) {
              return false;
            }
            if (!ValidateArtifactLiteral(stmt.var_decl.init_expr,
                                         artifact_it->second,
                                         mapping,
                                         ctx,
                                         scopes,
                                         current_artifact,
                                         error)) {
              return false;
            }
          }
        }
        std::string manifest_module;
        if (GetDlOpenManifestModule(stmt.var_decl.init_expr, ctx, &manifest_module)) {
          auto local_it = scopes.back().find(stmt.var_decl.name);
          if (local_it != scopes.back().end()) {
            local_it->second.dl_module = manifest_module;
          }
        }
        return true;
      }
      return true;
    case StmtKind::IfChain:
      for (const auto& branch : stmt.if_branches) {
        if (!CheckExpr(branch.first, ctx, scopes, current_artifact, error)) return false;
        if (!CheckBoolCondition(branch.first, ctx, scopes, current_artifact, error)) return false;
        scopes.emplace_back();
        for (const auto& child : branch.second) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      if (!stmt.else_branch.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.else_branch) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::IfStmt:
      if (!CheckExpr(stmt.if_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.if_cond, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.if_then) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      if (!stmt.if_else.empty()) {
        scopes.emplace_back();
        for (const auto& child : stmt.if_else) {
          if (!CheckStmt(child,
                         ctx,
                         type_params,
                         expected_return,
                         return_is_void,
                         loop_depth,
                         scopes,
                         current_artifact,
                         error)) {
            return false;
          }
        }
        scopes.pop_back();
      }
      return true;
    case StmtKind::WhileLoop:
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      scopes.emplace_back();
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth + 1,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      return true;
    case StmtKind::ForLoop: {
      scopes.emplace_back();
      if (!CheckExpr(stmt.loop_iter, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckBoolCondition(stmt.loop_cond, ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(stmt.loop_step, ctx, scopes, current_artifact, error)) return false;
      for (const auto& child : stmt.loop_body) {
        if (!CheckStmt(child,
                       ctx,
                       type_params,
                       expected_return,
                       return_is_void,
                       loop_depth + 1,
                       scopes,
                       current_artifact,
                       error)) {
          return false;
        }
      }
      scopes.pop_back();
      return true;
    }
    case StmtKind::Break:
      if (loop_depth == 0) {
        if (error) *error = "break used outside of loop";
        return false;
      }
      return true;
    case StmtKind::Skip:
      if (loop_depth == 0) {
        if (error) *error = "skip used outside of loop";
        return false;
      }
      return true;
  }
  return true;
}

bool IsIntegerTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128" ||
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

bool IsNumericTypeName(const std::string& name) {
  return IsIntegerTypeName(name) || IsFloatTypeName(name);
}

bool IsScalarType(const TypeRef& type) {
  return type.pointer_depth == 0 &&
         !type.is_proc &&
         type.dims.empty() &&
         type.type_args.empty();
}

bool CheckArrayLiteralShape(const Expr& expr,
                            const std::vector<TypeDim>& dims,
                            size_t dim_index,
                            std::string* error) {
  if (dim_index >= dims.size()) return true;
  const TypeDim& dim = dims[dim_index];
  if (!dim.has_size) return true;

  if (expr.kind == ExprKind::ListLiteral) {
    if (dim.size != 0) {
      if (error) *error = "array literal size does not match fixed dimensions";
      return false;
    }
    return true;
  }

  if (expr.kind != ExprKind::ArrayLiteral) {
    if (error) *error = "array literal size does not match fixed dimensions";
    return false;
  }
  if (expr.children.size() != dim.size) {
    if (error) *error = "array literal size does not match fixed dimensions";
    return false;
  }
  if (dim_index + 1 < dims.size()) {
    for (const auto& child : expr.children) {
      if (!CheckArrayLiteralShape(child, dims, dim_index + 1, error)) return false;
    }
  }
  return true;
}

bool CheckArrayLiteralElementTypes(const Expr& expr,
                                   const ValidateContext& ctx,
                                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                   const ArtifactDecl* current_artifact,
                                   const std::vector<TypeDim>& dims,
                                   size_t dim_index,
                                   const TypeRef& element_type,
                                   std::string* error) {
  if (expr.kind == ExprKind::ListLiteral) return true;
  if (expr.kind != ExprKind::ArrayLiteral) return true;
  if (dims.empty()) return true;

  if (dim_index + 1 >= dims.size()) {
    for (const auto& child : expr.children) {
      TypeRef child_type;
      if (!InferExprType(child, ctx, scopes, current_artifact, &child_type)) {
        if (error && error->empty()) *error = "array literal element type mismatch";
        return false;
      }
      if (!TypesCompatibleForExpr(element_type, child_type, child)) {
        if (error) *error = "array literal element type mismatch";
        return false;
      }
    }
    return true;
  }

  for (const auto& child : expr.children) {
    if (!CheckArrayLiteralElementTypes(child,
                                       ctx,
                                       scopes,
                                       current_artifact,
                                       dims,
                                       dim_index + 1,
                                       element_type,
                                       error)) {
      return false;
    }
  }
  return true;
}

bool CheckListLiteralElementTypes(const Expr& expr,
                                  const ValidateContext& ctx,
                                  const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                                  const ArtifactDecl* current_artifact,
                                  const TypeRef& list_type,
                                  std::string* error) {
  if (expr.kind != ExprKind::ListLiteral) return true;
  if (list_type.dims.empty()) return true;
  if (!list_type.dims.front().is_list) return true;

  TypeRef element_type;
  if (!CloneTypeRef(list_type, &element_type)) return false;
  if (!element_type.dims.empty()) {
    element_type.dims.erase(element_type.dims.begin());
  }

  for (const auto& child : expr.children) {
    TypeRef child_type;
    if (!InferExprType(child, ctx, scopes, current_artifact, &child_type)) {
      if (error && error->empty()) *error = "list literal element type mismatch";
      return false;
    }
    if (!TypesCompatibleForExpr(element_type, child_type, child)) {
      if (error) *error = "list literal element type mismatch";
      return false;
    }
  }
  return true;
}

bool CheckBoolCondition(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  TypeRef cond_type;
  if (InferExprType(expr, ctx, scopes, current_artifact, &cond_type)) {
    if (cond_type.pointer_depth != 0 || !IsBoolTypeName(cond_type.name)) {
      if (error) *error = "condition must be bool";
      return false;
    }
  }
  return true;
}

bool RequireScalar(const TypeRef& type, const std::string& op, std::string* error) {
  if (!IsScalarType(type)) {
    if (error) *error = "operator '" + op + "' requires scalar operands";
    return false;
  }
  return true;
}

bool CheckUnaryOpTypes(const Expr& expr,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  TypeRef operand;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &operand)) return true;
  if (!RequireScalar(operand, expr.op, error)) return false;

  const std::string op = expr.op.rfind("post", 0) == 0 ? expr.op.substr(4) : expr.op;
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

bool CheckBinaryOpTypes(const Expr& expr,
                        const ValidateContext& ctx,
                        const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                        const ArtifactDecl* current_artifact,
                        std::string* error) {
  TypeRef lhs;
  TypeRef rhs;
  if (!InferExprType(expr.children[0], ctx, scopes, current_artifact, &lhs)) return true;
  if (!InferExprType(expr.children[1], ctx, scopes, current_artifact, &rhs)) return true;

  if (!RequireScalar(lhs, expr.op, error)) return false;
  if (!RequireScalar(rhs, expr.op, error)) return false;
  if (!TypeEquals(lhs, rhs)) {
    const bool lhs_lit = IsIntegerLiteralExpr(expr.children[0]);
    const bool rhs_lit = IsIntegerLiteralExpr(expr.children[1]);
    const bool lhs_int = IsIntegerScalarTypeName(lhs.name);
    const bool rhs_int = IsIntegerScalarTypeName(rhs.name);
    if (!(lhs_lit && rhs_int) && !(rhs_lit && lhs_int)) {
      if (error) *error = "operator '" + expr.op + "' requires matching operand types";
      return false;
    }
  }

  const std::string& op = expr.op;
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

bool CheckFnLiteralAgainstType(const Expr& fn_expr,
                               const TypeRef& target_type,
                               std::string* error) {
  if (!target_type.is_proc) {
    if (error) *error = "fn literal requires procedure type";
    return false;
  }
  if (fn_expr.fn_params.size() != target_type.proc_params.size()) {
    if (error) {
      *error = "fn literal parameter count mismatch: expected " +
               std::to_string(target_type.proc_params.size()) + ", got " +
               std::to_string(fn_expr.fn_params.size());
    }
    return false;
  }
  for (size_t i = 0; i < fn_expr.fn_params.size(); ++i) {
    if (!TypeEquals(fn_expr.fn_params[i].type, target_type.proc_params[i])) {
      if (error) *error = "fn literal parameter type mismatch";
      return false;
    }
  }
  return true;
}

bool CheckExpr(const Expr& expr,
               const ValidateContext& ctx,
               const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
               const ArtifactDecl* current_artifact,
               std::string* error) {
  switch (expr.kind) {
    case ExprKind::Identifier:
      if (expr.text == "self") {
        if (!current_artifact) {
          if (error) *error = "self used outside of artifact method";
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        return true;
      }
      if (expr.text == "Core") {
        if (IsReservedModuleEnabled(ctx, "Core.DL") || IsReservedModuleEnabled(ctx, "Core.Os") ||
            IsReservedModuleEnabled(ctx, "Core.Fs") || IsReservedModuleEnabled(ctx, "Core.Log")) {
          return true;
        }
      }
      if (current_artifact && IsArtifactMemberName(current_artifact, expr.text)) {
        if (error) *error = "artifact members must be accessed via self: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (expr.text == "len" || expr.text == "str" || IsPrimitiveCastName(expr.text)) return true;
      if (FindLocal(scopes, expr.text)) return true;
      if (ctx.top_level.find(expr.text) != ctx.top_level.end()) {
        if (ctx.modules.find(expr.text) != ctx.modules.end()) {
          if (error) *error = "module is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        if (ctx.artifacts.find(expr.text) != ctx.artifacts.end()) {
          if (error) *error = "type is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        if (ctx.enum_types.find(expr.text) != ctx.enum_types.end()) {
          if (error) *error = "enum type is not a value: " + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        return true;
      }
      if (IsReservedModuleEnabled(ctx, expr.text)) {
        if (error) *error = "module is not a value: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (ctx.externs_by_module.find(expr.text) != ctx.externs_by_module.end()) {
        return true;
      }
      if (ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
        if (error) *error = "unqualified enum value: " + expr.text;
        PrefixErrorLocation(expr.line, expr.column, error);
        return false;
      }
      if (error) *error = "undeclared identifier: " + expr.text;
      PrefixErrorLocation(expr.line, expr.column, error);
      return false;
    case ExprKind::Literal:
      return true;
    case ExprKind::Unary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (expr.op == "++" || expr.op == "--" || expr.op == "post++" || expr.op == "post--") {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      return CheckUnaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Binary:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        if (!CheckAssignmentTarget(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      if (IsAssignOp(expr.op)) {
        TypeRef target_type;
        TypeRef value_type;
        bool have_target = InferExprType(expr.children[0], ctx, scopes, current_artifact, &target_type);
        bool have_value = InferExprType(expr.children[1], ctx, scopes, current_artifact, &value_type);
        if (expr.op != "=" && have_target && have_value) {
          if (!CheckCompoundAssignOp(expr.op, target_type, value_type, error)) return false;
          return true;
        }
        if (have_target && expr.children[1].kind == ExprKind::FnLiteral) {
          if (!CheckFnLiteralAgainstType(expr.children[1], target_type, error)) return false;
        }
        if (have_target &&
            (expr.children[1].kind == ExprKind::ArrayLiteral ||
             expr.children[1].kind == ExprKind::ListLiteral) &&
            !target_type.dims.empty()) {
          if (!CheckArrayLiteralShape(expr.children[1], target_type.dims, 0, error)) {
            return false;
          }
          TypeRef base_type;
          if (!CloneTypeRef(target_type, &base_type)) return false;
          base_type.dims.clear();
          if (!CheckArrayLiteralElementTypes(expr.children[1],
                                             ctx,
                                             scopes,
                                             current_artifact,
                                             target_type.dims,
                                             0,
                                             base_type,
                                             error)) {
            return false;
          }
          if (!CheckListLiteralElementTypes(expr.children[1],
                                            ctx,
                                            scopes,
                                            current_artifact,
                                            target_type,
                                            error)) {
            return false;
          }
        } else if (have_target &&
                   (expr.children[1].kind == ExprKind::ArrayLiteral ||
                    expr.children[1].kind == ExprKind::ListLiteral)) {
          if (error) *error = "array/list literal requires array or list type";
          return false;
        }
        if (have_target && have_value &&
            !TypesCompatibleForExpr(target_type, value_type, expr.children[1])) {
          if (error) *error = "assignment type mismatch";
          return false;
        }
        return true;
      }
      return CheckBinaryOpTypes(expr, ctx, scopes, current_artifact, error);
    case ExprKind::Call:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      for (const auto& arg : expr.args) {
        if (!CheckExpr(arg, ctx, scopes, current_artifact, error)) return false;
      }
      if (!CheckCallTarget(expr.children[0], expr.args.size(), ctx, scopes, current_artifact, error)) return false;
      if (IsIoPrintCallExpr(expr.children[0])) {
        if (expr.args.empty()) {
          if (error) *error = "call argument count mismatch for IO." + expr.children[0].text;
          return false;
        }
        if (expr.args.size() == 1) {
          TypeRef arg_type;
          if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
            if (error && error->empty()) *error = "IO.print expects scalar argument";
            return false;
          }
          if (arg_type.pointer_depth != 0 ||
              arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
            if (error) *error = "IO.print expects scalar argument";
            return false;
          }
          if (!(IsNumericTypeName(arg_type.name) ||
                IsBoolTypeName(arg_type.name) ||
                arg_type.name == "char" ||
                arg_type.name == "string")) {
            if (error) *error = "IO.print supports numeric, bool, char, or string";
            return false;
          }
        } else {
          if (!(expr.args[0].kind == ExprKind::Literal &&
                expr.args[0].literal_kind == LiteralKind::String)) {
            if (error) *error = "IO.print format call expects string literal as first argument";
            return false;
          }
          size_t placeholder_count = 0;
          if (!CountFormatPlaceholders(expr.args[0].text, &placeholder_count, error)) {
            return false;
          }
          const size_t value_count = expr.args.size() - 1;
          if (placeholder_count != value_count) {
            if (error) {
              *error = "IO.print format placeholder count mismatch: expected " +
                       std::to_string(placeholder_count) + ", got " +
                       std::to_string(value_count);
            }
            return false;
          }
          for (size_t i = 1; i < expr.args.size(); ++i) {
            TypeRef arg_type;
            if (!InferExprType(expr.args[i], ctx, scopes, current_artifact, &arg_type)) {
              if (error && error->empty()) *error = "IO.print format expects scalar arguments";
              return false;
            }
            if (arg_type.pointer_depth != 0 ||
                arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
              if (error) *error = "IO.print format expects scalar arguments";
              return false;
            }
            if (!(IsNumericTypeName(arg_type.name) ||
                  IsBoolTypeName(arg_type.name) ||
                  arg_type.name == "char" ||
                  arg_type.name == "string")) {
              if (error) *error = "IO.print supports numeric, bool, char, or string";
              return false;
            }
          }
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "len") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for len: expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (arg_type.dims.empty() && arg_type.name != "string") {
            if (error) *error = "len expects array, list, or string argument";
            return false;
          }
        } else {
          if (error && error->empty()) *error = "len expects array, list, or string argument";
          return false;
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier && expr.children[0].text == "str") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for str: expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (arg_type.pointer_depth != 0 ||
              (!IsNumericTypeName(arg_type.name) && !IsBoolTypeName(arg_type.name))) {
            if (error) *error = "str expects numeric or bool argument";
            return false;
          }
        } else {
          if (error && error->empty()) *error = "str expects numeric or bool argument";
          return false;
        }
      }
      if (expr.children[0].kind == ExprKind::Identifier &&
          IsPrimitiveCastName(expr.children[0].text)) {
        const std::string target = expr.children[0].text;
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for " + target + ": expected 1, got " +
                              std::to_string(expr.args.size());
          return false;
        }
        TypeRef arg_type;
        if (!InferExprType(expr.args[0], ctx, scopes, current_artifact, &arg_type)) {
          if (error && error->empty()) *error = target + " cast expects scalar argument";
          return false;
        }
        if (arg_type.is_proc || !arg_type.type_args.empty() || !arg_type.dims.empty()) {
          if (error) *error = target + " cast expects scalar argument";
          return false;
        }
        if (IsStringTypeName(arg_type.name) && !(target == "i32" || target == "f64")) {
          if (error) *error = target + " cast from string is unsupported";
          return false;
        }
      }
      if (!IsIoPrintCallExpr(expr.children[0]) &&
          !(expr.children[0].kind == ExprKind::Identifier &&
            (expr.children[0].text == "len" || expr.children[0].text == "str" ||
             IsPrimitiveCastName(expr.children[0].text)))) {
        if (!CheckCallArgTypes(expr, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::Member:
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            base.text == "IO" &&
            IsIoPrintName(expr.text)) {
          return true;
        }
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end()) {
          auto members_it = ctx.enum_members_by_type.find(base.text);
          if (members_it != ctx.enum_members_by_type.end()) {
            if (members_it->second.find(expr.text) == members_it->second.end()) {
              if (error) *error = "unknown enum member: " + base.text + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          }
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          if (const LocalInfo* local = FindLocal(scopes, base.text)) {
            if (!local->dl_module.empty()) {
              auto mod_it = ctx.externs_by_module.find(local->dl_module);
              if (mod_it != ctx.externs_by_module.end() &&
                  mod_it->second.find(expr.text) != mod_it->second.end()) {
                return true;
              }
            }
          }
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) *error = "unknown module member: " + base.text + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
            return true;
          }
          std::string module_name;
          if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
            TypeRef var_type;
            CallTargetInfo info;
            if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
                GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
              return true;
            }
            if (error) *error = "unknown module member: " + module_name + "." + expr.text;
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
        }
      }
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (expr.op == "." && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier) {
          if (const LocalInfo* local = FindLocal(scopes, base.text)) {
            if (!local->dl_module.empty()) {
              auto mod_it = ctx.externs_by_module.find(local->dl_module);
              if (mod_it != ctx.externs_by_module.end() &&
                  mod_it->second.find(expr.text) != mod_it->second.end()) {
                return true;
              }
            }
          }
          auto module_it = ctx.modules.find(base.text);
          if (module_it != ctx.modules.end()) {
            if (!FindModuleVar(module_it->second, expr.text) &&
                !FindModuleFunc(module_it->second, expr.text)) {
              if (error) *error = "unknown module member: " + base.text + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
            return true;
          }
          std::string module_name;
          if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
            TypeRef var_type;
            CallTargetInfo info;
            if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
                GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
              return true;
            }
            if (error) *error = "unknown module member: " + module_name + "." + expr.text;
            PrefixErrorLocation(expr.line, expr.column, error);
            return false;
          }
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name) && IsReservedModuleEnabled(ctx, module_name)) {
          TypeRef var_type;
          CallTargetInfo info;
          if (GetReservedModuleVarType(ctx, module_name, expr.text, &var_type) ||
              GetReservedModuleCallTarget(ctx, module_name, expr.text, &info)) {
            return true;
          }
          if (error) *error = "unknown module member: " + module_name + "." + expr.text;
          PrefixErrorLocation(expr.line, expr.column, error);
          return false;
        }
        TypeRef base_type;
        if (InferExprType(base, ctx, scopes, current_artifact, &base_type)) {
          auto artifact_it = ctx.artifacts.find(base_type.name);
          if (artifact_it != ctx.artifacts.end()) {
            const ArtifactDecl* artifact = artifact_it->second;
            if (!FindArtifactField(artifact, expr.text) &&
                !FindArtifactMethod(artifact, expr.text)) {
              if (error) *error = "unknown artifact member: " + base_type.name + "." + expr.text;
              PrefixErrorLocation(expr.line, expr.column, error);
              return false;
            }
          }
        }
      }
      if (expr.op == "::" && !expr.children.empty()) {
        const Expr& base = expr.children[0];
        if (base.kind == ExprKind::Identifier &&
            ctx.enum_types.find(base.text) != ctx.enum_types.end() &&
            ctx.enum_members.find(expr.text) != ctx.enum_members.end()) {
          if (error) *error = "enum members must be qualified with '.': " + base.text + "." + expr.text;
          return false;
        }
      }
      return true;
    case ExprKind::Index:
      if (!CheckExpr(expr.children[0], ctx, scopes, current_artifact, error)) return false;
      if (!CheckExpr(expr.children[1], ctx, scopes, current_artifact, error)) return false;
      {
        TypeRef base_type;
        if (InferExprType(expr.children[0], ctx, scopes, current_artifact, &base_type)) {
          if (base_type.dims.empty()) {
            if (error) *error = "indexing is only valid on arrays and lists";
            return false;
          }
        } else if (expr.children[0].kind == ExprKind::Literal) {
          if (error) *error = "indexing is only valid on arrays and lists";
          return false;
        }
      }
      if (expr.children[1].kind == ExprKind::Literal) {
        switch (expr.children[1].literal_kind) {
          case LiteralKind::Integer:
          case LiteralKind::Char:
            break;
          default:
            if (error) *error = "index must be an integer";
            return false;
        }
      } else {
        TypeRef index_type;
        if (InferExprType(expr.children[1], ctx, scopes, current_artifact, &index_type)) {
          if (!IsIntegerTypeName(index_type.name) && index_type.name != "char") {
            if (error) *error = "index must be an integer";
            return false;
          }
        }
      }
      return true;
    case ExprKind::ArrayLiteral:
    case ExprKind::ListLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::ArtifactLiteral:
      for (const auto& child : expr.children) {
        if (!CheckExpr(child, ctx, scopes, current_artifact, error)) return false;
      }
      for (const auto& field_value : expr.field_values) {
        if (!CheckExpr(field_value, ctx, scopes, current_artifact, error)) return false;
      }
      return true;
    case ExprKind::FnLiteral:
      return true;
  }
  return true;
}

bool StmtReturns(const Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Return:
      return true;
    case StmtKind::IfChain:
      if (stmt.if_branches.empty() || stmt.else_branch.empty()) return false;
      for (const auto& branch : stmt.if_branches) {
        if (!StmtsReturn(branch.second)) return false;
      }
      return StmtsReturn(stmt.else_branch);
    case StmtKind::IfStmt:
      if (stmt.if_then.empty() || stmt.if_else.empty()) return false;
      return StmtsReturn(stmt.if_then) && StmtsReturn(stmt.if_else);
    default:
      return false;
  }
}

bool StmtsReturn(const std::vector<Stmt>& stmts) {
  for (const auto& stmt : stmts) {
    if (StmtReturns(stmt)) return true;
  }
  return false;
}

bool CheckFunctionBody(const FuncDecl& fn,
                       const ValidateContext& ctx,
                       const std::unordered_set<std::string>& type_params,
                       const ArtifactDecl* current_artifact,
                       std::string* error) {
  std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
  scopes.emplace_back();
  std::unordered_set<std::string> param_names;
  const bool return_is_void = fn.return_type.name == "void";
  const bool is_main = (fn.name == "main" && fn.return_type.name == "i32");
  if (!CheckTypeRef(fn.return_type, ctx, type_params, TypeUse::Return, error)) return false;
  for (const auto& param : fn.params) {
    if (!param_names.insert(param.name).second) {
      if (error) *error = "duplicate parameter name: " + param.name;
      return false;
    }
    if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
    LocalInfo info;
    info.mutability = param.mutability;
    info.type = &param.type;
    if (!AddLocal(scopes, param.name, info, error)) return false;
  }
  for (const auto& stmt : fn.body) {
    if (!CheckStmt(stmt,
                   ctx,
                   type_params,
                   &fn.return_type,
                   return_is_void,
                   0,
                   scopes,
                   current_artifact,
                   error)) {
      return false;
    }
  }
  if (!return_is_void && !StmtsReturn(fn.body) && !is_main) {
    if (error) *error = "non-void function does not return on all paths";
    return false;
  }
  return true;
}

} // namespace

bool ValidateProgram(const Program& program, std::string* error) {
  ValidateContext ctx;
  if (program.decls.empty() && program.top_level_stmts.empty()) {
    if (error) *error = "program has no declarations or top-level statements";
    return false;
  }
  for (const auto& decl : program.decls) {
    const std::string* name_ptr = nullptr;
    switch (decl.kind) {
      case DeclKind::Import:
        if (!IsReservedImportPath(decl.import_decl.path)) {
          if (error) *error = "unsupported import path: " + decl.import_decl.path;
          return false;
        }
        ctx.reserved_imports.insert(decl.import_decl.path);
        if (decl.import_decl.has_alias && !decl.import_decl.alias.empty()) {
          ctx.reserved_import_aliases[decl.import_decl.alias] = decl.import_decl.path;
        }
        break;
      case DeclKind::Extern:
        if (decl.ext.has_module) {
          ctx.externs_by_module[decl.ext.module][decl.ext.name] = &decl.ext;
        } else {
          name_ptr = &decl.ext.name;
          ctx.externs[decl.ext.name] = &decl.ext;
        }
        break;
      case DeclKind::Enum:
        name_ptr = &decl.enm.name;
        {
          std::unordered_set<std::string> local_members;
          for (const auto& member : decl.enm.members) {
            if (!member.has_value) {
              if (error) *error = "enum member requires explicit value: " + member.name;
              return false;
            }
            if (!local_members.insert(member.name).second) {
              if (error) *error = "duplicate enum member: " + member.name;
              return false;
            }
            ctx.enum_members.insert(member.name);
          }
          ctx.enum_members_by_type[decl.enm.name] = std::move(local_members);
        }
        ctx.enum_types.insert(decl.enm.name);
        break;
      case DeclKind::Artifact:
        name_ptr = &decl.artifact.name;
        ctx.artifacts[decl.artifact.name] = &decl.artifact;
        ctx.artifact_generics[decl.artifact.name] = decl.artifact.generics.size();
        break;
      case DeclKind::Module:
        name_ptr = &decl.module.name;
        ctx.modules[decl.module.name] = &decl.module;
        break;
      case DeclKind::Function:
        name_ptr = &decl.func.name;
        ctx.functions[decl.func.name] = &decl.func;
        break;
      case DeclKind::Variable:
        name_ptr = &decl.var.name;
        ctx.globals[decl.var.name] = &decl.var;
        break;
    }
    if (name_ptr && !ctx.top_level.insert(*name_ptr).second) {
      if (error) *error = "duplicate top-level declaration: " + *name_ptr;
      return false;
    }
  }

  if (!program.top_level_stmts.empty()) {
    std::vector<std::unordered_map<std::string, LocalInfo>> scopes;
    scopes.emplace_back();
    std::unordered_set<std::string> type_params;
    TypeRef script_return;
    script_return.name = "i32";
    for (const auto& stmt : program.top_level_stmts) {
      if (stmt.kind == StmtKind::Return) {
        if (error) *error = "top-level return is not allowed";
        return false;
      }
      if (!CheckStmt(stmt,
                     ctx,
                     type_params,
                     &script_return,
                     false,
                     0,
                     scopes,
                     nullptr,
                     error)) {
        if (error && !error->empty()) {
          *error = "in top-level script: " + *error;
        }
        return false;
      }
    }
  }

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::Import:
        break;
      case DeclKind::Extern:
        {
          std::unordered_set<std::string> param_names;
          std::unordered_set<std::string> type_params;
          if (!CheckTypeRef(decl.ext.return_type, ctx, type_params, TypeUse::Return, error)) return false;
          for (const auto& param : decl.ext.params) {
            if (!param_names.insert(param.name).second) {
              if (error) *error = "duplicate extern parameter name: " + param.name;
              return false;
            }
            if (!CheckTypeRef(param.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
        }
        break;
      case DeclKind::Function:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.func.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(decl.func, ctx, type_params, nullptr, error)) {
            if (error && !error->empty()) {
              *error = "in function '" + decl.func.name + "': " + *error;
            }
            return false;
          }
        }
        break;
      case DeclKind::Artifact:
        {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(decl.artifact.generics, &type_params, error)) return false;
          std::unordered_set<std::string> names;
          for (const auto& field : decl.artifact.fields) {
            if (!names.insert(field.name).second) {
              if (error) *error = "duplicate artifact member: " + field.name;
              return false;
            }
            if (!CheckTypeRef(field.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
          for (const auto& method : decl.artifact.methods) {
            if (!names.insert(method.name).second) {
              if (error) *error = "duplicate artifact member: " + method.name;
              return false;
            }
          }
          for (const auto& method : decl.artifact.methods) {
            std::unordered_set<std::string> method_params;
            if (!CollectTypeParamsMerged(decl.artifact.generics,
                                         method.generics,
                                         &method_params,
                                         error)) {
              return false;
            }
            if (!CheckFunctionBody(method, ctx, method_params, &decl.artifact, error)) {
              if (error && !error->empty()) {
                *error = "in function '" + decl.artifact.name + "." + method.name + "': " + *error;
              }
              return false;
            }
          }
        }
        break;
      case DeclKind::Module:
        {
          std::unordered_set<std::string> names;
          for (const auto& var : decl.module.variables) {
            if (!names.insert(var.name).second) {
              if (error) *error = "duplicate module member: " + var.name;
              return false;
            }
            std::unordered_set<std::string> type_params;
            if (!CheckTypeRef(var.type, ctx, type_params, TypeUse::Value, error)) return false;
          }
          for (const auto& fn : decl.module.functions) {
            if (!names.insert(fn.name).second) {
              if (error) *error = "duplicate module member: " + fn.name;
              return false;
            }
          }
        }
        for (const auto& fn : decl.module.functions) {
          std::unordered_set<std::string> type_params;
          if (!CollectTypeParams(fn.generics, &type_params, error)) return false;
          if (!CheckFunctionBody(fn, ctx, type_params, nullptr, error)) {
            if (error && !error->empty()) {
              *error = "in function '" + decl.module.name + "." + fn.name + "': " + *error;
            }
            return false;
          }
        }
        break;
      case DeclKind::Enum:
      case DeclKind::Variable:
        if (decl.kind == DeclKind::Variable) {
          std::unordered_set<std::string> type_params;
          if (!CheckTypeRef(decl.var.type, ctx, type_params, TypeUse::Value, error)) return false;
        }
        break;
    }
  }

  return true;
}

bool ValidateProgramFromString(const std::string& text, std::string* error) {
  Program program;
  if (!ParseProgramFromString(text, &program, error)) return false;
  return ValidateProgram(program, error);
}

} // namespace Simple::Lang
