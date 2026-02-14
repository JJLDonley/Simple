#include "lang_validate.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lang_parser.h"
#include "lang_reserved.h"

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

bool InferExprType(const Expr& expr,
                   const ValidateContext& ctx,
                   const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                   const ArtifactDecl* current_artifact,
                   TypeRef* out);
bool CloneTypeRef(const TypeRef& src, TypeRef* out);
bool IsIntegerLiteralExpr(const Expr& expr);
bool IsFloatLiteralExpr(const Expr& expr);
bool IsIntegerScalarTypeName(const std::string& name);
bool IsBoolTypeName(const std::string& name);
bool IsNumericTypeName(const std::string& name);
bool IsScalarType(const TypeRef& type);
void PrefixErrorLocation(uint32_t line, uint32_t column, std::string* error);
bool IsReservedModuleEnabled(const ValidateContext& ctx, const std::string& name);
bool ResolveReservedModuleName(const ValidateContext& ctx,
                               const std::string& name,
                               std::string* out);
bool GetModuleNameFromExpr(const Expr& base, std::string* out);
bool GetCallTargetInfo(const Expr& callee,
                       const ValidateContext& ctx,
                       const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
                       const ArtifactDecl* current_artifact,
                       CallTargetInfo* out,
                       std::string* error);
bool IsCallbackType(const TypeRef& type);
const LocalInfo* FindLocal(
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    const std::string& name);

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

bool IsPrimitiveCastName(const std::string& name) {
  if (name == "string") return false;
  return kPrimitiveTypes.find(name) != kPrimitiveTypes.end();
}

bool GetAtCastTargetName(const std::string& name, std::string* out_target) {
  if (name.size() < 2 || name[0] != '@') return false;
  const std::string target = name.substr(1);
  if (!IsPrimitiveCastName(target)) return false;
  if (out_target) *out_target = target;
  return true;
}

bool IsIoPrintName(const std::string& name);
bool IsIoPrintCallExpr(const Expr& callee, const ValidateContext& ctx) {
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (!IsIoPrintName(callee.text)) return false;
  if (callee.children[0].kind == ExprKind::Identifier && callee.children[0].text == "IO") return true;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  std::string resolved;
  return ResolveReservedModuleName(ctx, module_name, &resolved) && resolved == "Core.IO";
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

bool ResolveDlModuleForIdentifier(
    const std::string& ident,
    const ValidateContext& ctx,
    const std::vector<std::unordered_map<std::string, LocalInfo>>& scopes,
    std::string* out_module) {
  if (!out_module) return false;
  if (const LocalInfo* local = FindLocal(scopes, ident)) {
    if (!local->dl_module.empty()) {
      *out_module = local->dl_module;
      return true;
    }
  }
  auto global_it = ctx.globals.find(ident);
  if (global_it != ctx.globals.end() &&
      global_it->second &&
      global_it->second->has_init_expr) {
    if (GetDlOpenManifestModule(global_it->second->init_expr, ctx, out_module)) {
      return true;
    }
  }
  return false;
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

size_t EditDistance(const std::string& a, const std::string& b) {
  std::vector<size_t> prev(b.size() + 1);
  std::vector<size_t> cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[b.size()];
}

std::vector<std::string> ReservedModuleMembers(const std::string& resolved) {
  if (resolved == "Core.IO") {
    return {"print", "println", "buffer_new", "buffer_len", "buffer_fill", "buffer_copy"};
  }
  if (resolved == "Core.Math") return {"abs", "min", "max", "PI"};
  if (resolved == "Core.Time") return {"mono_ns", "wall_ns"};
  if (resolved == "Core.DL") {
    return {"open", "sym", "close", "last_error", "call_i32", "call_i64", "call_f32", "call_f64",
            "call_str0", "supported"};
  }
  if (resolved == "Core.OS") {
    return {"args_count", "args_get", "env_get", "cwd_get", "time_mono_ns", "time_wall_ns",
            "sleep_ms", "is_linux", "is_macos", "is_windows", "has_dl"};
  }
  if (resolved == "Core.FS") return {"open", "close", "read", "write"};
  if (resolved == "Core.Log") return {"log"};
  return {};
}

std::vector<std::string> ModuleMembers(const ModuleDecl* module) {
  std::vector<std::string> out;
  if (!module) return out;
  out.reserve(module->variables.size() + module->functions.size());
  for (const auto& v : module->variables) out.push_back(v.name);
  for (const auto& f : module->functions) out.push_back(f.name);
  return out;
}

std::string UnknownMemberErrorWithSuggestion(const std::string& module_name,
                                             const std::string& member,
                                             const std::vector<std::string>& candidates) {
  std::string out = "unknown module member: " + module_name + "." + member;
  if (candidates.empty()) return out;
  size_t best_dist = static_cast<size_t>(-1);
  std::string best;
  for (const auto& c : candidates) {
    const size_t d = EditDistance(member, c);
    if (d < best_dist) {
      best_dist = d;
      best = c;
    }
  }
  if (!best.empty() && best_dist <= 3) {
    out += " (did you mean '" + best + "'?)";
  }
  return out;
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
  out.proc_is_callback = false;
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

bool CloneElementType(const TypeRef& container, TypeRef* out) {
  if (!out) return false;
  if (container.dims.empty()) return false;
  if (!CloneTypeRef(container, out)) return false;
  out->dims.erase(out->dims.begin());
  return true;
}

bool GetReservedModuleVarType(const ValidateContext& ctx,
                              const std::string& module,
                              const std::string& member,
                              TypeRef* out) {
  std::string resolved;
  if (!ResolveReservedModuleName(ctx, module, &resolved)) return false;
  if (resolved == "Core.Math" && member == "PI") {
    if (out) *out = MakeSimpleType("f64");
    return true;
  }
  if (resolved == "Core.DL" && member == "supported") {
    if (out) *out = MakeSimpleType("bool");
    return true;
  }
  if (resolved == "Core.OS" &&
      (member == "is_linux" || member == "is_macos" || member == "is_windows" || member == "has_dl")) {
    if (out) *out = MakeSimpleType("bool");
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
  if (resolved == "Core.Math") {
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
  if (resolved == "Core.Time") {
    if (member == "mono_ns" || member == "wall_ns") {
      out->return_type = MakeSimpleType("i64");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
  }
  if (resolved == "Core.IO") {
    if (member == "buffer_new") {
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeListType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "buffer_len") {
      out->params.push_back(MakeListType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "buffer_fill") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
      out->return_mutability = Mutability::Mutable;
      return true;
    }
    if (member == "buffer_copy") {
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeListType("i32"));
      out->params.push_back(MakeSimpleType("i32"));
      out->return_type = MakeSimpleType("i32");
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
  if (resolved == "Core.OS") {
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
  if (resolved == "Core.FS") {
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
  out->proc_is_callback = src.proc_is_callback;
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
    if (a.proc_is_callback || b.proc_is_callback) return true;
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

bool IsIntegerScalarTypeName(const std::string& name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "i128" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "u128";
}

bool IsFloatScalarTypeName(const std::string& name) {
  return name == "f32" || name == "f64";
}

bool IsLiteralCompatibleWithScalarType(const Expr& expr, const TypeRef& expected) {
  if (expected.pointer_depth != 0 || expected.is_proc || !expected.type_args.empty() ||
      !expected.dims.empty()) {
    return false;
  }
  if (IsIntegerLiteralExpr(expr) && IsIntegerScalarTypeName(expected.name)) return true;
  if (IsFloatLiteralExpr(expr) && IsFloatScalarTypeName(expected.name)) return true;
  return false;
}

bool TypesCompatibleForExpr(const TypeRef& expected, const TypeRef& actual, const Expr& expr) {
  if (TypeEquals(expected, actual)) return true;
  if (actual.pointer_depth == 0 && !actual.is_proc && actual.type_args.empty() &&
      actual.dims.empty() &&
      IsLiteralCompatibleWithScalarType(expr, expected)) {
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
    if (param.proc_is_callback || arg.proc_is_callback) return true;
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
    if (type.proc_is_callback) {
      if (!type.proc_params.empty() || type.proc_return) {
        if (error) *error = "callback type cannot declare parameter or return types";
        PrefixErrorLocation(type.line, type.column, error);
        return false;
      }
      return true;
    }
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
