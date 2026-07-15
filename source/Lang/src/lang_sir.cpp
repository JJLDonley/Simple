#include "IRE/sir_emitter.h"
#include "IRE/capture_analysis.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CAST/parser.h"
#include "GEN/specializer.h"
#include "lang_reserved.h"
#include "lang_version.h"
#include "platform/platform.h"
#include "RAST/reserved_resolution.h"
#include "TAST/type_checker.h"
#include "TAST/types.h"
#include "native/registry.h"
#include "TAST/control_flow.h"
#include "intrinsic_ids.h"

namespace Simple::Lang {
namespace {

struct EmitState {
  std::ostringstream* out = nullptr;
  std::string* error = nullptr;

  std::unordered_map<std::string, std::string> string_consts;
  std::vector<std::string> const_lines;
  uint32_t string_index = 0;

  std::unordered_map<std::string, TypeRef> local_types;
  std::unordered_map<std::string, std::string> local_dl_modules;
  std::unordered_map<std::string, uint16_t> local_indices;
  std::unordered_set<std::string> captured_locals;
  struct CaptureInfo {
    TypeRef type;
    uint16_t index = 0;
  };
  std::unordered_map<std::string, CaptureInfo> current_upvalues;
  std::unordered_map<std::string, std::vector<std::pair<std::string, TypeRef>>> lambda_captures;
  uint16_t next_local = 0;

  std::unordered_map<std::string, uint32_t> func_ids;
  std::unordered_map<std::string, TypeRef> func_returns;
  std::unordered_map<std::string, std::vector<TypeRef>> func_params;
  std::unordered_set<std::string> async_funcs;
  std::unordered_map<std::string, std::string> module_func_names;
  std::unordered_map<std::string, std::string> artifact_method_names;
  uint32_t base_func_count = 0;
  uint32_t lambda_counter = 0;
  std::vector<FuncDecl> lambda_funcs;
  std::unordered_map<std::string, std::string> proc_sig_names;
  std::vector<std::string> proc_sig_lines;
  LibraryModuleSet reserved_imports;
  LibraryModuleAliasMap reserved_import_aliases;
  std::unordered_set<std::string> using_reserved_modules;
  std::unordered_set<std::string> using_modules;
  std::unordered_set<std::string> imported_modules;
  std::unordered_map<std::string, std::string> module_aliases;
  std::unordered_map<std::string, std::string> extern_ids;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> extern_ids_by_module;
  std::unordered_map<std::string, std::vector<TypeRef>> extern_params;
  std::unordered_map<std::string, TypeRef> extern_returns;
  std::unordered_map<std::string, std::unordered_map<std::string, std::vector<TypeRef>>> extern_params_by_module;
  std::unordered_map<std::string, std::unordered_map<std::string, TypeRef>> extern_returns_by_module;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dl_call_import_ids_by_module;
  std::unordered_map<std::string, uint32_t> global_indices;
  std::unordered_map<std::string, TypeRef> global_types;
  std::unordered_map<std::string, Mutability> global_mutability;
  std::unordered_map<std::string, std::string> global_dl_modules;
  std::string global_init_func_name;
  std::vector<const VarDecl*> global_decls;

  struct ImportItem {
    std::string name;
    std::string module;
    std::string symbol;
    std::string sig_name;
    uint32_t flags = 0;
    std::vector<TypeRef> params;
    TypeRef ret;
  };
  std::vector<ImportItem> imports;

  struct FieldLayout {
    uint32_t offset = 0;
    std::string name;
    TypeRef type;
    std::string sir_type;
  };
  struct ArtifactLayout {
    uint32_t size = 0;
    std::vector<FieldLayout> fields;
    std::unordered_map<std::string, size_t> field_index;
  };

  std::unordered_map<std::string, const ArtifactDecl*> artifacts;
  std::unordered_map<std::string, ArtifactLayout> artifact_layouts;
  std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_values;

  uint32_t temp_counter = 0;
  struct AbiFieldPath {
    std::vector<std::string> path;
    TypeRef type;
    std::string abi_name;
  };
  struct AbiTypeInfo {
    std::string name;
    std::vector<AbiFieldPath> fields;
  };
  std::unordered_map<std::string, AbiTypeInfo> abi_types;
  std::unordered_map<std::string, std::string> abi_type_by_artifact;

  uint32_t stack_cur = 0;
  uint32_t stack_max = 0;
  bool saw_return = false;
  std::string current_func;

  uint32_t label_counter = 0;
  struct LoopLabels {
    std::string break_label;
    std::string continue_label;
  };
  std::vector<LoopLabels> loop_stack;
};

struct FuncItem {
  const FuncDecl* decl = nullptr;
  std::string emit_name;
  std::string display_name;
  bool has_self = false;
  TypeRef self_type;
  const std::vector<Stmt>* script_body = nullptr;
};

bool PushStack(EmitState& st, uint32_t count);
bool PopStack(EmitState& st, uint32_t count);
bool AddStringConst(EmitState& st, const std::string& value, std::string* out_name);
bool CloneTypeRef(const TypeRef& src, TypeRef* out);
bool CloneCallReturn(const EmitState& state,
                     const std::string& function_name,
                     const TypeRef& declared,
                     TypeRef* out) {
  if (state.async_funcs.find(function_name) == state.async_funcs.end()) {
    return CloneTypeRef(declared, out);
  }
  *out = TypeRef{};
  out->name = "Promise";
  TypeRef value;
  if (!CloneTypeRef(declared, &value)) return false;
  out->type_args.push_back(std::move(value));
  return true;
}

void EmitDirectCallOpcode(EmitState& state,
                          const std::string& function_name,
                          uint32_t function_id,
                          size_t argument_count) {
  if (state.async_funcs.find(function_name) != state.async_funcs.end()) {
    (*state.out) << "  future.make " << function_id << " " << argument_count << "\n";
  } else {
    (*state.out) << "  call " << function_id << " " << argument_count << "\n";
  }
}

bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error);

bool IsIntegerLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Integer;
}

bool IsFloatLiteralExpr(const Expr& expr) {
  return expr.kind == ExprKind::Literal && expr.literal_kind == LiteralKind::Float;
}

bool IsAbiScalarType(const TypeRef& type, const EmitState& st) {
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (type.name == "string") return true;
  if (TAST::IsNumericScalarTypeName(type.name) || type.name == "bool" || type.name == "char") return true;
  if (st.enum_values.find(type.name) != st.enum_values.end()) return true;
  return false;
}

bool ArtifactHasNestedArtifacts(const std::string& name, const EmitState& st) {
  auto it = st.artifacts.find(name);
  if (it == st.artifacts.end()) return false;
  const ArtifactDecl* art = it->second;
  for (const auto& field : art->fields) {
    if (field.type.pointer_depth > 0) continue;
    if (st.artifacts.find(field.type.name) != st.artifacts.end()) return true;
  }
  return false;
}

bool NeedsAbiFlattenType(const TypeRef& type, const EmitState& st) {
  if (type.pointer_depth > 0) return false;
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (st.artifacts.find(type.name) == st.artifacts.end()) return false;
  return ArtifactHasNestedArtifacts(type.name, st);
}

bool CollectAbiFieldsForArtifact(const std::string& name,
                                 const EmitState& st,
                                 std::vector<std::string>& prefix,
                                 std::vector<EmitState::AbiFieldPath>* out_fields,
                                 std::unordered_set<std::string>* visiting,
                                 std::string* error) {
  auto it = st.artifacts.find(name);
  if (it == st.artifacts.end()) {
    if (error) *error = "unknown artifact for ABI flattening: " + name;
    return false;
  }
  if (visiting && !visiting->insert(name).second) {
    if (error) *error = "recursive artifact ABI flattening is unsupported: " + name;
    return false;
  }
  const ArtifactDecl* art = it->second;
  for (const auto& field : art->fields) {
    if (field.type.is_proc || !field.type.type_args.empty() || !field.type.dims.empty()) {
      if (error) *error = "unsupported ABI field type in artifact: " + field.name;
      return false;
    }
    if (field.type.pointer_depth > 0) {
      EmitState::AbiFieldPath item;
      item.path = prefix;
      item.path.push_back(field.name);
      if (!CloneTypeRef(field.type, &item.type)) return false;
      out_fields->push_back(std::move(item));
      continue;
    }
    if (st.artifacts.find(field.type.name) != st.artifacts.end()) {
      prefix.push_back(field.name);
      if (!CollectAbiFieldsForArtifact(field.type.name, st, prefix, out_fields, visiting, error)) return false;
      prefix.pop_back();
      continue;
    }
    if (!IsAbiScalarType(field.type, st)) {
      if (error) *error = "unsupported ABI field type in artifact: " + field.name;
      return false;
    }
    EmitState::AbiFieldPath item;
    item.path = prefix;
    item.path.push_back(field.name);
    if (!CloneTypeRef(field.type, &item.type)) return false;
    if (st.enum_values.find(item.type.name) != st.enum_values.end()) {
      item.type.name = "i32";
      item.type.type_args.clear();
      item.type.dims.clear();
      item.type.pointer_depth = 0;
      item.type.is_proc = false;
      item.type.proc_params.clear();
      item.type.proc_return.reset();
    }
    out_fields->push_back(std::move(item));
  }
  if (visiting) visiting->erase(name);
  return true;
}

bool EnsureAbiTypeForArtifact(EmitState& st,
                              const std::string& name,
                              std::string* out_abi_name,
                              std::string* error) {
  auto existing = st.abi_type_by_artifact.find(name);
  if (existing != st.abi_type_by_artifact.end()) {
    if (out_abi_name) *out_abi_name = existing->second;
    return true;
  }
  if (!ArtifactHasNestedArtifacts(name, st)) {
    if (out_abi_name) *out_abi_name = name;
    return true;
  }
  EmitState::AbiTypeInfo info;
  info.name = "__abi_" + name;
  std::vector<std::string> prefix;
  std::unordered_set<std::string> visiting;
  if (!CollectAbiFieldsForArtifact(name, st, prefix, &info.fields, &visiting, error)) return false;
  for (auto& field : info.fields) {
    std::string flat;
    for (size_t i = 0; i < field.path.size(); ++i) {
      if (i) flat += "__";
      flat += field.path[i];
    }
    field.abi_name = flat;
  }
  st.abi_type_by_artifact[name] = info.name;
  st.abi_types.emplace(info.name, std::move(info));
  if (out_abi_name) *out_abi_name = st.abi_type_by_artifact[name];
  return true;
}

bool GetAtCastTargetName(const std::string& name, std::string* out_target) {
  if (name.size() < 2 || name[0] != '@') return false;
  const std::string target = name.substr(1);
  if (!TAST::IsPrimitiveCastName(target)) return false;
  if (out_target) *out_target = target;
  return true;
}

enum class CastVmKind : uint8_t { Invalid, I32, I64, F32, F64 };

CastVmKind GetCastVmKind(const std::string& type_name) {
  if (type_name == "i8" || type_name == "i16" || type_name == "i32" ||
      type_name == "u8" || type_name == "u16" || type_name == "u32" ||
      type_name == "bool" || type_name == "char") {
    return CastVmKind::I32;
  }
  if (type_name == "i64" || type_name == "u64") return CastVmKind::I64;
  if (type_name == "f32") return CastVmKind::F32;
  if (type_name == "f64") return CastVmKind::F64;
  return CastVmKind::Invalid;
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
}

bool CountFormatPlaceholders(const std::string& fmt,
                             size_t* out_count,
                             std::vector<std::string>* out_segments,
                             std::string* error) {
  if (!out_count) return false;
  *out_count = 0;
  if (out_segments) out_segments->clear();
  size_t segment_start = 0;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{') {
      if (i + 1 >= fmt.size() || fmt[i + 1] != '}') {
        if (error) *error = "invalid format string: expected '{}' placeholder";
        return false;
      }
      if (out_segments) out_segments->push_back(fmt.substr(segment_start, i - segment_start));
      ++(*out_count);
      ++i;
      segment_start = i + 1;
      continue;
    }
    if (fmt[i] == '}') {
      if (error) *error = "invalid format string: unmatched '}'";
      return false;
    }
  }
  if (out_segments) out_segments->push_back(fmt.substr(segment_start));
  return true;
}

TypeRef MakeTypeRef(const char* name) {
  TypeRef out;
  out.name = name;
  return out;
}

std::string NormalizeCoreDlMember(const std::string& name) {
  return NormalizeSystemFFIMemberName(name);
}

std::string ResolveImportModule(const std::string& module) {
  if (const auto replacement = LegacyRuntimeModuleReplacementView(module)) {
    return std::string(*replacement);
  }
  return module;
}

bool GetModuleNameFromExpr(const Expr& base, std::string* out) {
  if (!out) return false;
  if (base.kind == ExprKind::Identifier) {
    *out = base.text;
    return true;
  }
  if (base.kind == ExprKind::Member && base.op == "." && !base.children.empty()) {
    std::string prefix;
    if (!GetModuleNameFromExpr(base.children[0], &prefix)) return false;
    *out = prefix + "." + base.text;
    return true;
  }
  return false;
}

bool ResolveReservedModuleId(const EmitState& st,
                             const std::string& name,
                             LibraryModuleId* out) {
  if (!out) return false;
  if (auto info = ParseLibraryImportPath(name)) {
    LibraryModuleId id{info->root, info->module_index};
    if (st.reserved_imports.find(id) != st.reserved_imports.end()) {
      *out = id;
      return true;
    }
  }
  if (auto module = ParseCanonicalLibraryModule(name)) {
    if (st.reserved_imports.find(*module) != st.reserved_imports.end()) {
      *out = *module;
      return true;
    }
  }
  auto it = st.reserved_import_aliases.find(name);
  if (it != st.reserved_import_aliases.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool IsNativeReservedModule(const std::string& canonical) {
  const auto id = ParseCanonicalLibraryModule(canonical);
  return id && !ToNativeModule(*id).empty();
}

bool ResolveUsingReservedMember(const EmitState& st,
                                const std::string& member,
                                std::string* out_module) {
  bool found = false;
  std::string result;
  for (const auto& module : st.using_reserved_modules) {
    const auto id = ParseCanonicalLibraryModule(module);
    if (!id) continue;
    const std::string normalized_member =
        (id->root == LibraryRoot::System &&
         static_cast<SystemModule>(id->module_index) == SystemModule::FFI)
            ? NormalizeCoreDlMember(member)
            : member;
    if (!RAST::IsReservedModuleFunction(module, normalized_member) &&
        !RAST::GetReservedModuleVarType(module, normalized_member, nullptr)) {
      continue;
    }
    if (found) return false;
    found = true;
    result = module;
  }
  if (!found) return false;
  if (out_module) *out_module = std::move(result);
  return true;
}

bool ResolveUsingModuleExternMember(const EmitState& st,
                                    const std::string& member,
                                    std::string* out_module) {
  bool found = false;
  std::string result;
  for (const auto& module : st.using_modules) {
    auto mod_it = st.extern_ids_by_module.find(module);
    if (mod_it == st.extern_ids_by_module.end()) continue;
    if (mod_it->second.find(member) == mod_it->second.end()) continue;
    if (found) return false;
    found = true;
    result = module;
  }
  if (!found) return false;
  if (out_module) *out_module = std::move(result);
  return true;
}

bool IsIoPrintCallExpr(const Expr& callee, const EmitState& st) {
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (!IsIoPrintName(callee.text)) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  LibraryModuleId resolved{};
  return ResolveReservedModuleId(st, module_name, &resolved) &&
         resolved.root == LibraryRoot::Standard &&
         static_cast<StandardModule>(resolved.module_index) == StandardModule::IO;
}

bool HostIsLinux() {
  return Simple::Platform::HostOperatingSystem() == Simple::Platform::OperatingSystem::Linux;
}

bool HostIsMacOs() {
  return Simple::Platform::HostOperatingSystem() == Simple::Platform::OperatingSystem::macOS;
}

bool HostIsWindows() {
  return Simple::Platform::HostOperatingSystem() == Simple::Platform::OperatingSystem::Windows;
}

bool HostHasDl() {
  return HostIsLinux() || HostIsMacOs();
}

bool IsCoreDlOpenCallExpr(const Expr& expr, const EmitState& st) {
  if (expr.kind != ExprKind::Call || expr.children.empty()) return false;
  const Expr& callee = expr.children[0];
  if (callee.kind != ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  LibraryModuleId resolved{};
  if (!ResolveReservedModuleId(st, module_name, &resolved)) return false;
  return resolved.root == LibraryRoot::System &&
         static_cast<SystemModule>(resolved.module_index) == SystemModule::FFI &&
         ParseMember(SystemModule::FFI, NormalizeCoreDlMember(callee.text)) == SystemMember(SystemFFIMember::Open);
}

bool GetDlOpenManifestModule(const Expr& expr, const EmitState& st, std::string* out_module) {
  if (!out_module) return false;
  if (!IsCoreDlOpenCallExpr(expr, st)) return false;
  if (expr.args.size() != 2) return false;
  if (expr.args[1].kind != ExprKind::Identifier) return false;
  const std::string& module = expr.args[1].text;
  auto mod_it = st.extern_returns_by_module.find(module);
  if (mod_it == st.extern_returns_by_module.end() || mod_it->second.empty()) return false;
  *out_module = module;
  return true;
}

bool ResolveDlModuleForIdentifier(const std::string& ident,
                                  const EmitState& st,
                                  std::string* out_module) {
  if (!out_module) return false;
  auto dl_local_it = st.local_dl_modules.find(ident);
  if (dl_local_it != st.local_dl_modules.end()) {
    *out_module = dl_local_it->second;
    return true;
  }
  auto dl_global_it = st.global_dl_modules.find(ident);
  if (dl_global_it != st.global_dl_modules.end()) {
    *out_module = dl_global_it->second;
    return true;
  }
  for (const auto* glob : st.global_decls) {
    if (!glob || glob->name != ident || !glob->has_init_expr) continue;
    if (GetDlOpenManifestModule(glob->init_expr, st, out_module)) {
      return true;
    }
  }
  return false;
}

bool FindDlHandleGlobalForModule(const EmitState& st,
                                 const std::string& module,
                                 std::string* out_global) {
  if (!out_global) return false;
  const auto alias_it = st.module_aliases.find(module);
  const std::string target_module = alias_it == st.module_aliases.end() ? module : alias_it->second;
  for (const auto& entry : st.global_dl_modules) {
    if (entry.second == module || entry.second == target_module) {
      *out_global = entry.first;
      return true;
    }
  }
  for (const auto* glob : st.global_decls) {
    if (!glob || !glob->has_init_expr) continue;
    std::string manifest;
    if (GetDlOpenManifestModule(glob->init_expr, st, &manifest) &&
        (manifest == module || manifest == target_module)) {
      *out_global = glob->name;
      return true;
    }
  }
  return false;
}

bool GetCoreDlSymImportId(const EmitState& st, std::string* out_id) {
  if (!out_id) return false;
  for (const auto& entry : st.extern_ids_by_module) {
    LibraryModuleId resolved{};
    if (!ResolveReservedModuleId(st, entry.first, &resolved) ||
        resolved.root != LibraryRoot::System ||
        static_cast<SystemModule>(resolved.module_index) != SystemModule::FFI) continue;
    auto it = entry.second.find("sym");
    if (it != entry.second.end()) {
      *out_id = it->second;
      return true;
    }
  }
  return false;
}

bool IsSupportedDlAbiType(const TypeRef& type, const EmitState& st, bool allow_void) {
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) return false;
  if (type.pointer_depth > 0) return true;
  if (allow_void && type.name == "void") return true;
  if (type.name == "i8" || type.name == "i16" || type.name == "i32" || type.name == "i64" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64" ||
      type.name == "f32" || type.name == "f64" || type.name == "bool" || type.name == "char" ||
      type.name == "string") {
    return true;
  }
  if (st.enum_values.find(type.name) != st.enum_values.end()) return true;
  if (st.abi_types.find(type.name) != st.abi_types.end()) return true;
  if (st.artifacts.find(type.name) != st.artifacts.end()) {
    std::unordered_set<std::string> visiting;
    std::function<bool(const std::string&)> check_struct = [&](const std::string& name) -> bool {
      if (!visiting.insert(name).second) return false;
      auto it = st.artifacts.find(name);
      if (it == st.artifacts.end()) {
        visiting.erase(name);
        return false;
      }
      const ArtifactDecl* art = it->second;
      for (const auto& field : art->fields) {
        if (field.type.is_proc || !field.type.type_args.empty() || !field.type.dims.empty()) {
          visiting.erase(name);
          return false;
        }
        if (field.type.pointer_depth > 0) continue;
        if (field.type.name == "i8" || field.type.name == "i16" || field.type.name == "i32" ||
            field.type.name == "i64" || field.type.name == "u8" || field.type.name == "u16" ||
            field.type.name == "u32" || field.type.name == "u64" || field.type.name == "f32" ||
            field.type.name == "f64" || field.type.name == "bool" || field.type.name == "char" ||
            field.type.name == "string") {
          continue;
        }
        if (st.enum_values.find(field.type.name) != st.enum_values.end()) continue;
        if (st.artifacts.find(field.type.name) != st.artifacts.end()) {
          if (!check_struct(field.type.name)) {
            visiting.erase(name);
            return false;
          }
          continue;
        }
        visiting.erase(name);
        return false;
      }
      visiting.erase(name);
      return true;
    };
    return check_struct(type.name);
  }
  return false;
}

bool GetPrintAnyTagForType(const TypeRef& type, uint32_t* out, std::string* error) {
  if (!out) return false;
  if (type.is_proc || !type.type_args.empty() || !type.dims.empty()) {
    if (error) *error = "Standard.IO.print expects scalar value";
    return false;
  }
  const std::string& name = type.name;
  if (name == "i8") { *out = Simple::VM::kPrintAnyTagI8; return true; }
  if (name == "i16") { *out = Simple::VM::kPrintAnyTagI16; return true; }
  if (name == "i32") { *out = Simple::VM::kPrintAnyTagI32; return true; }
  if (name == "i64") { *out = Simple::VM::kPrintAnyTagI64; return true; }
  if (name == "u8") { *out = Simple::VM::kPrintAnyTagU8; return true; }
  if (name == "u16") { *out = Simple::VM::kPrintAnyTagU16; return true; }
  if (name == "u32") { *out = Simple::VM::kPrintAnyTagU32; return true; }
  if (name == "u64") { *out = Simple::VM::kPrintAnyTagU64; return true; }
  if (name == "f32") { *out = Simple::VM::kPrintAnyTagF32; return true; }
  if (name == "f64") { *out = Simple::VM::kPrintAnyTagF64; return true; }
  if (name == "bool") { *out = Simple::VM::kPrintAnyTagBool; return true; }
  if (name == "char") { *out = Simple::VM::kPrintAnyTagChar; return true; }
  if (name == "string") { *out = Simple::VM::kPrintAnyTagString; return true; }
  if (error) *error = "Standard.IO.print supports numeric, bool, char, or string";
  return false;
}

bool EmitPrintAnyValue(EmitState& st,
                       const Expr& arg_expr,
                       const TypeRef& arg_type,
                       std::string* error) {
  if (!EmitExpr(st, arg_expr, &arg_type, error)) return false;
  uint32_t tag = 0;
  if (!GetPrintAnyTagForType(arg_type, &tag, error)) return false;
  (*st.out) << "  const i32 " << static_cast<int32_t>(tag) << "\n";
  PushStack(st, 1);
  (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicPrintAny << "\n";
  PopStack(st, 2);
  return true;
}

bool EmitPrintNewline(EmitState& st, std::string* error) {
  (void)error;
  std::string newline_name;
  if (!AddStringConst(st, "\n", &newline_name)) return false;
  (*st.out) << "  const string " << newline_name << "\n";
  PushStack(st, 1);
  (*st.out) << "  const i32 " << static_cast<int32_t>(Simple::VM::kPrintAnyTagString) << "\n";
  PushStack(st, 1);
  (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicPrintAny << "\n";
  PopStack(st, 2);
  return true;
}

bool IsSupportedType(const TypeRef& type) {
  if (!type.type_args.empty()) {
    return type.name == "Promise" && type.type_args.size() == 1 &&
           type.pointer_depth == 0 && !type.is_proc;
  }
  if (type.pointer_depth > 0) return true;
  if (type.is_proc) return true;
  if (!type.dims.empty()) {
    if (type.name == "void") return false;
    return true;
  }
  if (type.name == "void") return true;
  if (TAST::IsNumericScalarTypeName(type.name) || type.name == "bool" || type.name == "char" || type.name == "string") return true;
  return true;
}

bool CloneTypeRef(const TypeRef& src, TypeRef* out) {
  if (!out) return false;
  out->name = src.name;
  out->pointer_depth = src.pointer_depth;
  out->type_args.clear();
  out->type_args.reserve(src.type_args.size());
  for (const auto& arg : src.type_args) {
    TypeRef cloned;
    if (!CloneTypeRef(arg, &cloned)) return false;
    out->type_args.push_back(std::move(cloned));
  }
  out->dims = src.dims;
  out->is_optional_syntax = src.is_optional_syntax;
  out->is_proc = src.is_proc;
  out->proc_return_mutability = src.proc_return_mutability;
  out->proc_params.clear();
  out->proc_params.reserve(src.proc_params.size());
  for (const auto& param : src.proc_params) {
    TypeRef cloned;
    if (!CloneTypeRef(param, &cloned)) return false;
    out->proc_params.push_back(std::move(cloned));
  }
  if (src.proc_return) {
    TypeRef cloned;
    if (!CloneTypeRef(*src.proc_return, &cloned)) return false;
    out->proc_return = std::make_unique<TypeRef>(std::move(cloned));
  } else {
    out->proc_return.reset();
  }
  return true;
}

std::string EscapeStringLiteral(const std::string& value, std::string* error) {
  (void)error;
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          static const char kHex[] = "0123456789ABCDEF";
          unsigned char byte = static_cast<unsigned char>(ch);
          out += "\\x";
          out.push_back(kHex[(byte >> 4) & 0xF]);
          out.push_back(kHex[byte & 0xF]);
          break;
        }
        out.push_back(ch);
        break;
    }
  }
  return out;
}

bool ParseIntegerLiteralText(const std::string& text, int64_t* out) {
  if (!out) return false;
  try {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      *out = static_cast<int64_t>(std::stoull(text.substr(2), nullptr, 16));
      return true;
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
      uint64_t value = 0;
      for (size_t i = 2; i < text.size(); ++i) {
        char c = text[i];
        if (c != '0' && c != '1') return false;
        value = (value << 1) | static_cast<uint64_t>(c - '0');
      }
      *out = static_cast<int64_t>(value);
      return true;
    }
    *out = static_cast<int64_t>(std::stoll(text, nullptr, 10));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::string NewLabel(EmitState& st, const std::string& prefix) {
  return prefix + std::to_string(st.label_counter++);
}

const char* NormalizeNumericOpType(const std::string& name) {
  if (name == "i8" || name == "i16" || name == "i32" || name == "char") return "i32";
  if (name == "u8" || name == "u16" || name == "u32") return "u32";
  if (name == "i64") return "i64";
  if (name == "u64") return "u64";
  if (name == "f32") return "f32";
  if (name == "f64") return "f64";
  return nullptr;
}

const char* NormalizeBitwiseOpType(const std::string& name) {
  if (name == "i8" || name == "i16" || name == "i32" || name == "char") return "i32";
  if (name == "u8" || name == "u16" || name == "u32") return "i32";
  if (name == "i64" || name == "u64") return "i64";
  return nullptr;
}

const char* IncOpForType(const std::string& name) {
  if (name == "i8") return "inc i8";
  if (name == "i16") return "inc i16";
  if (name == "i32" || name == "char" || name == "bool") return "inc i32";
  if (name == "i64") return "inc i64";
  if (name == "u8") return "inc u8";
  if (name == "u16") return "inc u16";
  if (name == "u32") return "inc u32";
  if (name == "u64") return "inc u64";
  if (name == "f32") return "inc f32";
  if (name == "f64") return "inc f64";
  return nullptr;
}

const char* DecOpForType(const std::string& name) {
  if (name == "i8") return "dec i8";
  if (name == "i16") return "dec i16";
  if (name == "i32" || name == "char" || name == "bool") return "dec i32";
  if (name == "i64") return "dec i64";
  if (name == "u8") return "dec u8";
  if (name == "u16") return "dec u16";
  if (name == "u32") return "dec u32";
  if (name == "u64") return "dec u64";
  if (name == "f32") return "dec f32";
  if (name == "f64") return "dec f64";
  return nullptr;
}

const char* VmOpSuffixForType(const TypeRef& type, const EmitState& st) {
  if (type.pointer_depth > 0) return "i64";
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "string") return "ref";
  if (type.name == "bool" || type.name == "char" || type.name == "i8" || type.name == "i16" || type.name == "i32" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32") {
    return "i32";
  }
  if (type.name == "i64" || type.name == "u64") return "i64";
  if (type.name == "f32") return "f32";
  if (type.name == "f64") return "f64";
  if (st.enum_values.find(type.name) != st.enum_values.end()) return "i32";
  return "ref";
}

const char* VmTypeNameForElement(const TypeRef& type, const EmitState& st) {
  const char* suffix = VmOpSuffixForType(type, st);
  if (!suffix) return nullptr;
  if (std::string(suffix) == "i32") return "i32";
  if (std::string(suffix) == "i64") return "i64";
  if (std::string(suffix) == "f32") return "f32";
  if (std::string(suffix) == "f64") return "f64";
  return "ref";
}

bool CloneElementType(const TypeRef& container, TypeRef* out) {
  if (!out) return false;
  if (container.dims.empty()) return false;
  if (!CloneTypeRef(container, out)) return false;
  out->dims.erase(out->dims.begin());
  return true;
}

bool AllocateTempLocal(EmitState& st,
                       const TypeRef& type,
                       std::string* out_name,
                       uint16_t* out_index,
                       std::string* error) {
  std::string name = "__abi_tmp" + std::to_string(st.temp_counter++);
  if (st.local_indices.find(name) != st.local_indices.end()) {
    if (error) *error = "internal error: temp local collision";
    return false;
  }
  uint16_t index = st.next_local++;
  st.local_indices.emplace(name, index);
  TypeRef cloned;
  if (!CloneTypeRef(type, &cloned)) return false;
  st.local_types.emplace(name, std::move(cloned));
  if (out_name) *out_name = name;
  if (out_index) *out_index = index;
  return true;
}

const EmitState::AbiTypeInfo* FindAbiTypeForArtifact(const EmitState& st,
                                                     const std::string& name) {
  auto it = st.abi_type_by_artifact.find(name);
  if (it == st.abi_type_by_artifact.end()) return nullptr;
  auto info_it = st.abi_types.find(it->second);
  if (info_it == st.abi_types.end()) return nullptr;
  return &info_it->second;
}

bool EmitLoadFieldPathFromLocal(EmitState& st,
                                uint16_t src_index,
                                const std::string& root_type,
                                const std::vector<std::string>& path,
                                TypeRef* out_leaf,
                                std::string* error) {
  if (path.empty()) {
    if (error) *error = "internal error: empty ABI field path";
    return false;
  }
  (*st.out) << "  ldloc " << src_index << "\n";
  PushStack(st, 1);
  std::string current = root_type;
  TypeRef leaf_type;
  for (size_t i = 0; i < path.size(); ++i) {
    auto layout_it = st.artifact_layouts.find(current);
    if (layout_it == st.artifact_layouts.end()) {
      if (error) *error = "unknown artifact layout for '" + current + "'";
      return false;
    }
    const auto& layout = layout_it->second;
    auto field_it = layout.field_index.find(path[i]);
    if (field_it == layout.field_index.end()) {
      if (error) *error = "unknown field '" + path[i] + "' in '" + current + "'";
      return false;
    }
    const TypeRef& field_type = layout.fields[field_it->second].type;
    (*st.out) << "  ldfld " << current << "." << path[i] << "\n";
    if (!CloneTypeRef(field_type, &leaf_type)) return false;
    if (i + 1 < path.size()) {
      current = field_type.name;
    }
  }
  if (out_leaf) *out_leaf = std::move(leaf_type);
  return true;
}

bool EmitAbiPackArtifactArg(EmitState& st,
                            const Expr& value,
                            const TypeRef& orig_type,
                            const EmitState::AbiTypeInfo& abi,
                            std::string* error) {
  if (!EmitExpr(st, value, &orig_type, error)) return false;
  std::string src_name;
  uint16_t src_index = 0;
  if (!AllocateTempLocal(st, orig_type, &src_name, &src_index, error)) return false;
  (*st.out) << "  stloc " << src_index << "\n";
  PopStack(st, 1);

  TypeRef abi_type;
  abi_type.name = abi.name;
  std::string abi_name;
  uint16_t abi_index = 0;
  (*st.out) << "  newobj " << abi.name << "\n";
  PushStack(st, 1);
  if (!AllocateTempLocal(st, abi_type, &abi_name, &abi_index, error)) return false;
  (*st.out) << "  stloc " << abi_index << "\n";
  PopStack(st, 1);

  for (const auto& field : abi.fields) {
    (*st.out) << "  ldloc " << abi_index << "\n";
    PushStack(st, 1);
    if (!EmitLoadFieldPathFromLocal(st, src_index, orig_type.name, field.path, nullptr, error)) return false;
    (*st.out) << "  stfld " << abi.name << "." << field.abi_name << "\n";
    PopStack(st, 2);
  }

  (*st.out) << "  ldloc " << abi_index << "\n";
  PushStack(st, 1);
  return true;
}

bool EmitAbiInflateReturn(EmitState& st,
                          const EmitState::AbiTypeInfo& abi,
                          const std::string& orig_type,
                          std::string* error) {
  TypeRef abi_type;
  abi_type.name = abi.name;
  std::string abi_name;
  uint16_t abi_index = 0;
  if (!AllocateTempLocal(st, abi_type, &abi_name, &abi_index, error)) return false;
  (*st.out) << "  stloc " << abi_index << "\n";
  PopStack(st, 1);

  TypeRef root_type;
  root_type.name = orig_type;
  std::string root_name;
  uint16_t root_index = 0;
  (*st.out) << "  newobj " << orig_type << "\n";
  PushStack(st, 1);
  if (!AllocateTempLocal(st, root_type, &root_name, &root_index, error)) return false;
  (*st.out) << "  stloc " << root_index << "\n";
  PopStack(st, 1);

  std::unordered_map<std::string, uint16_t> nested_locals;
  std::unordered_map<std::string, std::string> nested_types;

  for (const auto& field : abi.fields) {
    std::string current_type = orig_type;
    std::string prefix_key;
    for (size_t i = 0; i + 1 < field.path.size(); ++i) {
      if (!prefix_key.empty()) prefix_key += ".";
      prefix_key += field.path[i];
      if (nested_locals.find(prefix_key) != nested_locals.end()) {
        current_type = nested_types[prefix_key];
        continue;
      }
      auto layout_it = st.artifact_layouts.find(current_type);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "unknown artifact layout for '" + current_type + "'";
        return false;
      }
      auto field_it = layout_it->second.field_index.find(field.path[i]);
      if (field_it == layout_it->second.field_index.end()) {
        if (error) *error = "unknown field '" + field.path[i] + "' in '" + current_type + "'";
        return false;
      }
      const TypeRef& field_type = layout_it->second.fields[field_it->second].type;
      TypeRef nested_type;
      if (!CloneTypeRef(field_type, &nested_type)) return false;
      std::string nested_name;
      uint16_t nested_index = 0;
      (*st.out) << "  newobj " << field_type.name << "\n";
      PushStack(st, 1);
      if (!AllocateTempLocal(st, nested_type, &nested_name, &nested_index, error)) return false;
      (*st.out) << "  stloc " << nested_index << "\n";
      PopStack(st, 1);

      uint16_t parent_index = root_index;
      std::string parent_type = orig_type;
      if (!prefix_key.empty()) {
        size_t dot = prefix_key.rfind('.');
        if (dot != std::string::npos) {
          std::string parent_key = prefix_key.substr(0, dot);
          parent_index = nested_locals[parent_key];
          parent_type = nested_types[parent_key];
        }
      }
      (*st.out) << "  ldloc " << parent_index << "\n";
      PushStack(st, 1);
      (*st.out) << "  ldloc " << nested_index << "\n";
      PushStack(st, 1);
      (*st.out) << "  stfld " << parent_type << "." << field.path[i] << "\n";
      PopStack(st, 2);

      nested_locals[prefix_key] = nested_index;
      nested_types[prefix_key] = field_type.name;
      current_type = field_type.name;
    }
  }

  for (const auto& field : abi.fields) {
    uint16_t parent_index = root_index;
    std::string parent_type = orig_type;
    if (field.path.size() > 1) {
      std::string prefix_key;
      for (size_t i = 0; i + 1 < field.path.size(); ++i) {
        if (!prefix_key.empty()) prefix_key += ".";
        prefix_key += field.path[i];
      }
      parent_index = nested_locals[prefix_key];
      parent_type = nested_types[prefix_key];
    }
    (*st.out) << "  ldloc " << parent_index << "\n";
    PushStack(st, 1);
    (*st.out) << "  ldloc " << abi_index << "\n";
    PushStack(st, 1);
    (*st.out) << "  ldfld " << abi.name << "." << field.abi_name << "\n";
    (*st.out) << "  stfld " << parent_type << "." << field.path.back() << "\n";
    PopStack(st, 2);
  }

  (*st.out) << "  ldloc " << root_index << "\n";
  PushStack(st, 1);
  return true;
}

bool EmitDynamicDlCallByHandleGlobal(EmitState& st,
                                     const std::string& handle_global,
                                     const std::string& dl_module,
                                     const std::string& symbol,
                                     const std::vector<Expr>& args,
                                     std::string* error) {
  auto global_it = st.global_indices.find(handle_global);
  if (global_it == st.global_indices.end()) {
    if (error) *error = "missing dynamic DL handle global: " + handle_global;
    return false;
  }
  auto params_mod_it = st.extern_params_by_module.find(dl_module);
  auto returns_mod_it = st.extern_returns_by_module.find(dl_module);
  if (params_mod_it == st.extern_params_by_module.end() ||
      returns_mod_it == st.extern_returns_by_module.end()) {
    if (error) *error = "unknown dynamic DL manifest module: " + dl_module;
    return false;
  }
  auto params_it = params_mod_it->second.find(symbol);
  auto ret_it = returns_mod_it->second.find(symbol);
  if (params_it == params_mod_it->second.end() || ret_it == returns_mod_it->second.end()) {
    if (error) *error = "unknown dynamic symbol: " + dl_module + "." + symbol;
    return false;
  }
  const auto& params = params_it->second;
  if (args.size() != params.size()) {
    if (error) *error = "call argument count mismatch for dynamic symbol '" + dl_module + "." + symbol + "'";
    return false;
  }
  auto call_mod_it = st.dl_call_import_ids_by_module.find(dl_module);
  if (call_mod_it == st.dl_call_import_ids_by_module.end()) {
    if (error) *error = "missing dynamic DL call import module: " + dl_module;
    return false;
  }
  auto call_id_it = call_mod_it->second.find(symbol);
  if (call_id_it == call_mod_it->second.end()) {
    if (error) *error = "missing dynamic DL call import: " + dl_module + "." + symbol;
    return false;
  }
  std::string sym_import_id;
  if (!GetCoreDlSymImportId(st, &sym_import_id)) {
    if (error) *error = "missing DL.sym import for dynamic symbol calls";
    return false;
  }
  const EmitState::AbiTypeInfo* abi_ret = nullptr;
  if (NeedsAbiFlattenType(ret_it->second, st)) {
    abi_ret = FindAbiTypeForArtifact(st, ret_it->second.name);
    if (!abi_ret) {
      if (error) *error = "missing ABI type for dynamic return '" + symbol + "'";
      return false;
    }
  }
  (*st.out) << "  ldglob " << global_it->second << "\n";
  PushStack(st, 1);
  std::string symbol_name;
  if (!AddStringConst(st, symbol, &symbol_name)) return false;
  (*st.out) << "  const string " << symbol_name << "\n";
  PushStack(st, 1);
  (*st.out) << "  call " << sym_import_id << " 2\n";
  PopStack(st, 2);
  PushStack(st, 1);
  uint32_t abi_arg_count = 1;
  for (size_t i = 0; i < params.size(); ++i) {
    const EmitState::AbiTypeInfo* abi_param = nullptr;
    if (NeedsAbiFlattenType(params[i], st)) {
      abi_param = FindAbiTypeForArtifact(st, params[i].name);
      if (!abi_param) {
        if (error) *error = "missing ABI type for dynamic param '" + symbol + "'";
        return false;
      }
    }
    if (abi_param) {
      if (!EmitAbiPackArtifactArg(st, args[i], params[i], *abi_param, error)) return false;
    } else {
      if (!EmitExpr(st, args[i], &params[i], error)) return false;
    }
    ++abi_arg_count;
  }
  if (abi_arg_count > 255) {
    if (error) *error = "dynamic DL call has too many ABI parameters";
    return false;
  }
  (*st.out) << "  call " << call_id_it->second << " " << abi_arg_count << "\n";
  PopStack(st, abi_arg_count);
  if (ret_it->second.name != "void") PushStack(st, 1);
  if (abi_ret) {
    if (!EmitAbiInflateReturn(st, *abi_ret, ret_it->second.name, error)) return false;
  }
  return true;
}

uint32_t FieldSizeForType(const TypeRef& type) {
  if (type.is_proc) return 4;
  if (!type.dims.empty()) return 4;
  if (type.name == "string") return 4;
  if (type.name == "bool" || type.name == "char" || type.name == "i8" || type.name == "i16" ||
      type.name == "i32" || type.name == "u8" || type.name == "u16" || type.name == "u32") {
    return 4;
  }
  if (type.name == "i64" || type.name == "u64" || type.name == "f64") return 8;
  if (type.name == "f32") return 4;
  return 4;
}

uint32_t FieldAlignForType(const TypeRef& type) {
  uint32_t size = FieldSizeForType(type);
  if (size == 0) return 1;
  if (size > 8) return 8;
  return size;
}

uint32_t AlignTo(uint32_t value, uint32_t align) {
  if (align <= 1) return value;
  uint32_t mask = align - 1;
  return (value + mask) & ~mask;
}

std::string FieldSirTypeName(const TypeRef& type, const EmitState& st) {
  if (type.pointer_depth > 0) return "i64";
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "string") return "string";
  if (TAST::IsNumericScalarTypeName(type.name) || type.name == "bool" || type.name == "char") return type.name;
  if (st.artifacts.find(type.name) != st.artifacts.end()) return "ref";
  if (st.abi_types.find(type.name) != st.abi_types.end()) return "ref";
  if (st.enum_values.find(type.name) != st.enum_values.end()) return "i32";
  return "ref";
}

std::string SigTypeNameFromType(const TypeRef& type, const EmitState& st, std::string* error) {
  if (type.pointer_depth > 0) return "i64";
  if (type.is_proc) return "ref";
  if (!type.dims.empty()) return "ref";
  if (type.name == "void") return "void";
  if (type.name == "string") return "string";
  if (type.name == "Promise" && type.type_args.size() == 1) return "ref";
  if (TAST::IsNumericScalarTypeName(type.name) || type.name == "bool" || type.name == "char") return type.name;
  if (st.artifacts.find(type.name) != st.artifacts.end()) return type.name;
  if (st.abi_types.find(type.name) != st.abi_types.end()) return type.name;
  if (st.enum_values.find(type.name) != st.enum_values.end()) return "i32";
  if (error) *error = "unsupported type in signature: " + type.name;
  return {};
}

std::string GetProcSigName(EmitState& st,
                           const TypeRef& proc_type,
                           std::string* error) {
  std::string local_error;
  std::string* err = error ? error : &local_error;
  std::ostringstream key;
  std::string ret = "void";
  if (proc_type.proc_return) {
    ret = SigTypeNameFromType(*proc_type.proc_return, st, err);
    if (!err->empty()) return {};
  }
  key << ret << "|";
  for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
    if (i > 0) key << ",";
    std::string param = SigTypeNameFromType(proc_type.proc_params[i], st, err);
    if (!err->empty()) return {};
    key << param;
  }
  std::string key_str = key.str();
  auto it = st.proc_sig_names.find(key_str);
  if (it != st.proc_sig_names.end()) return it->second;

  std::string name = "sig_proc_" + std::to_string(st.proc_sig_names.size());
  std::ostringstream line;
  line << "  sig " << name << ": (";
  for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
    if (i > 0) line << ", ";
    std::string param = SigTypeNameFromType(proc_type.proc_params[i], st, err);
    if (!err->empty()) return {};
    line << param;
  }
  line << ") -> " << ret;
  st.proc_sig_names.emplace(std::move(key_str), name);
  st.proc_sig_lines.push_back(line.str());
  return name;
}

bool PushStack(EmitState& st, uint32_t count) {
  st.stack_cur += count;
  if (st.stack_cur > st.stack_max) st.stack_max = st.stack_cur;
  return true;
}

bool PopStack(EmitState& st, uint32_t count) {
  if (st.stack_cur < count) st.stack_cur = 0;
  else st.stack_cur -= count;
  return true;
}

bool EmitDup(EmitState& st) {
  (*st.out) << "  dup\n";
  return PushStack(st, 1);
}

bool EmitDup2(EmitState& st) {
  (*st.out) << "  dup2\n";
  return PushStack(st, 2);
}

bool AddStringConst(EmitState& st, const std::string& value, std::string* out_name) {
  auto it = st.string_consts.find(value);
  if (it != st.string_consts.end()) {
    *out_name = it->second;
    return true;
  }
  std::string error;
  std::string escaped = EscapeStringLiteral(value, &error);
  if (!error.empty()) {
    if (st.error) *st.error = error;
    return false;
  }
  std::string name = "str" + std::to_string(st.string_index++);
  st.string_consts.emplace(value, name);
  st.const_lines.push_back("  const " + name + " string \"" + escaped + "\"");
  *out_name = name;
  return true;
}

bool AddGlobalInitConst(EmitState& st, const std::string& global_name, const TypeRef& type, std::string* out_name) {
  if (!out_name) return false;
  auto make_name = [&]() {
    return "__ginit_" + global_name;
  };
  if (type.name == "f32") {
    std::string name = make_name();
    st.const_lines.push_back("  const " + name + " f32 0.0");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "f64") {
    std::string name = make_name();
    st.const_lines.push_back("  const " + name + " f64 0.0");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "string") {
    std::string name = make_name();
    st.const_lines.push_back("  const " + name + " string \"\"");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "i8" || type.name == "i16" || type.name == "i32" || type.name == "i64" ||
      type.name == "u8" || type.name == "u16" || type.name == "u32" || type.name == "u64" ||
      type.name == "bool" || type.name == "char") {
    std::string name = make_name();
    // IR global init constants currently support string/f32/f64 const-id lookup.
    st.const_lines.push_back("  const " + name + " f64 0.0");
    *out_name = std::move(name);
    return true;
  }
  if (type.name == "void") return false;
  // Keep non-scalar globals verifier-initialized; __global_init performs real init when present.
  std::string name = make_name();
  st.const_lines.push_back("  const " + name + " f64 0.0");
  *out_name = std::move(name);
  return true;
}

bool InferExprType(const Expr& expr,
                   const EmitState& st,
                   TypeRef* out,
                   std::string* error);
bool IsDirectFnLiteralCall(const Expr& expr) {
  return expr.kind == ExprKind::Call && !expr.children.empty() &&
         expr.children[0].kind == ExprKind::FnLiteral;
}

bool InferBinaryOperandTypes(const Expr& expr,
                             const EmitState& st,
                             TypeRef* left,
                             TypeRef* right,
                             std::string* error);
bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error);
bool EmitDefaultInit(EmitState& st, const TypeRef& type, std::string* error);
bool EmitInactivePayload(EmitState& st, const TypeRef& type, std::string* error);

bool InferLiteralType(const Expr& expr, TypeRef* out) {
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
  return false;
}

bool TypeEquals(const TypeRef& a, const TypeRef& b) {
  if (a.pointer_depth != b.pointer_depth) return false;
  if (a.is_optional_syntax != b.is_optional_syntax) return false;
  if (a.is_proc != b.is_proc) return false;
  if (a.is_proc) {
    if (a.proc_params.size() != b.proc_params.size()) return false;
    for (size_t i = 0; i < a.proc_params.size(); ++i) {
      if (!TypeEquals(a.proc_params[i], b.proc_params[i])) return false;
    }
    if (a.proc_return && b.proc_return) {
      if (!TypeEquals(*a.proc_return, *b.proc_return)) return false;
    } else if (a.proc_return || b.proc_return) {
      return false;
    }
    return true;
  }
  if (a.name != b.name) return false;
  if (a.type_args.size() != b.type_args.size()) return false;
  for (size_t i = 0; i < a.type_args.size(); ++i) {
    if (!TypeEquals(a.type_args[i], b.type_args[i])) return false;
  }
  if (a.dims.size() != b.dims.size()) return false;
  for (size_t i = 0; i < a.dims.size(); ++i) {
    if (a.dims[i].is_list != b.dims[i].is_list) return false;
    if (a.dims[i].has_size != b.dims[i].has_size) return false;
    if (a.dims[i].has_size && a.dims[i].size != b.dims[i].size) return false;
  }
  return true;
}

struct TaggedTypeInfo {
  TaggedArtifactKind kind = TaggedArtifactKind::None;
  const TypeRef* value_type = nullptr;
  const TypeRef* error_type = nullptr;
  const ArtifactDecl* artifact = nullptr;
};

const TypeRef* FindArtifactFieldType(const ArtifactDecl& artifact,
                                     const std::string& name) {
  for (const auto& field : artifact.fields) {
    if (field.name == name) return &field.type;
  }
  return nullptr;
}

bool ResolveTaggedType(const TypeRef& type,
                       const EmitState& st,
                       TaggedTypeInfo* out) {
  if (!out || type.pointer_depth != 0 || !type.dims.empty() || type.is_proc) return false;
  *out = {};
  if (TAST::IsOptionalType(type)) {
    out->kind = TaggedArtifactKind::Optional;
    out->value_type = TAST::OptionalValueType(type);
    return out->value_type != nullptr;
  }
  if (type.name == "Result" && type.type_args.size() == 2) {
    out->kind = TaggedArtifactKind::Result;
    out->value_type = &type.type_args[0];
    out->error_type = &type.type_args[1];
    return true;
  }
  const auto artifact_it = st.artifacts.find(type.name);
  if (artifact_it == st.artifacts.end() ||
      artifact_it->second->tagged_kind == TaggedArtifactKind::None) {
    return false;
  }
  out->artifact = artifact_it->second;
  out->kind = out->artifact->tagged_kind;
  out->value_type = FindArtifactFieldType(*out->artifact, "value");
  if (out->kind == TaggedArtifactKind::Result) {
    out->error_type = FindArtifactFieldType(*out->artifact, "error");
  }
  return out->value_type &&
         (out->kind != TaggedArtifactKind::Result || out->error_type);
}

bool IsSwitchPattern(const SwitchBranch& branch) {
  return branch.pattern_kind != SwitchPatternKind::None;
}

bool GetSwitchBranchValueExpr(const SwitchBranch& branch,
                              const Expr** out_expr,
                              std::string* error) {
  if (!out_expr) return false;
  *out_expr = nullptr;
  if (branch.is_block) {
    if (branch.block.empty() ||
        branch.block.back().kind != StmtKind::Return ||
        !branch.block.back().has_return_expr) {
      if (error) *error = "switch branch block must end with a return value";
      return false;
    }
    *out_expr = &branch.block.back().expr;
    return true;
  }
  if (!branch.has_inline_value) {
    if (error) *error = "switch branch requires a value";
    return false;
  }
  *out_expr = &branch.value;
  return true;
}

bool InferSwitchExprType(const Expr& expr,
                         const EmitState& st,
                         TypeRef* out,
                         std::string* error) {
  if (!out) return false;
  if (expr.children.empty()) {
    if (error) *error = "invalid switch expression";
    return false;
  }
  TypeRef subject_type;
  if (!InferExprType(expr.children[0], st, &subject_type, error)) return false;
  if (expr.switch_branches.empty()) {
    if (error) *error = "switch requires at least one branch";
    return false;
  }
  TaggedTypeInfo subject_tagged;
  const bool tagged_subject = ResolveTaggedType(subject_type, st, &subject_tagged);
  bool uses_patterns = false;
  for (const auto& branch : expr.switch_branches) {
    uses_patterns = uses_patterns || IsSwitchPattern(branch);
  }
  if (uses_patterns && !tagged_subject) {
    if (error) *error = "structural switch patterns require optional or Result subject";
    return false;
  }
  size_t default_count = 0;
  bool has_type = false;
  TypeRef common;
  for (const auto& branch : expr.switch_branches) {
    const TypeRef* binding_type = nullptr;
    if (uses_patterns) {
      if (!IsSwitchPattern(branch)) {
        if (error) *error = "cannot mix structural patterns with switch conditions";
        return false;
      }
      if (subject_tagged.kind == TaggedArtifactKind::Optional &&
          branch.pattern_kind == SwitchPatternKind::Present) {
        binding_type = subject_tagged.value_type;
      } else if (subject_tagged.kind == TaggedArtifactKind::Result &&
                 branch.pattern_kind == SwitchPatternKind::Tagged) {
        if (branch.pattern_field == "value") binding_type = subject_tagged.value_type;
        if (branch.pattern_field == "error") binding_type = subject_tagged.error_type;
      }
    } else if (branch.is_default) {
      default_count++;
    } else {
      TypeRef cond_type;
      if (!InferExprType(branch.condition, st, &cond_type, error)) return false;
      if (cond_type.name != "bool") {
        if (error) *error = "switch condition must be bool";
        return false;
      }
    }
    const Expr* value_expr = nullptr;
    if (!GetSwitchBranchValueExpr(branch, &value_expr, error)) return false;
    EmitState branch_st = st;
    if (binding_type) {
      TypeRef cloned;
      if (!CloneTypeRef(*binding_type, &cloned)) return false;
      branch_st.local_types[branch.pattern_binding] = std::move(cloned);
    }
    if (branch.is_block) {
      for (size_t stmt_index = 0; stmt_index + 1 < branch.block.size(); ++stmt_index) {
        const Stmt& prefix = branch.block[stmt_index];
        if (prefix.kind != StmtKind::VarDecl) continue;
        TypeRef cloned;
        if (!CloneTypeRef(prefix.var_decl.type, &cloned)) return false;
        branch_st.local_types[prefix.var_decl.name] = std::move(cloned);
      }
    }
    TypeRef value_type;
    if (!InferExprType(*value_expr, branch_st, &value_type, error)) return false;
    if (!has_type) {
      if (!CloneTypeRef(value_type, &common)) return false;
      has_type = true;
    } else if (!TypeEquals(common, value_type)) {
      if (error) *error = "switch branch type mismatch";
      return false;
    }
  }
  if (!uses_patterns && default_count != 1) {
    if (error) *error = "switch must have exactly one default branch";
    return false;
  }
  return CloneTypeRef(common, out);
}

bool InferExprType(const Expr& expr,
                   const EmitState& st,
                   TypeRef* out,
                   std::string* error) {
  if (!out) return false;
  switch (expr.kind) {
    case ExprKind::Identifier: {
      auto it = st.local_types.find(expr.text);
      if (it != st.local_types.end()) {
        return CloneTypeRef(it->second, out);
      }
      auto upvalue_it = st.current_upvalues.find(expr.text);
      if (upvalue_it != st.current_upvalues.end()) {
        return CloneTypeRef(upvalue_it->second.type, out);
      }
      auto git = st.global_types.find(expr.text);
      if (git != st.global_types.end()) {
        return CloneTypeRef(git->second, out);
      }
      if (error) *error = "unknown local '" + expr.text + "'";
      return false;
    }
    case ExprKind::Literal:
      return InferLiteralType(expr, out);
    case ExprKind::FormatString:
      out->name = "string";
      out->type_args.clear();
      out->dims.clear();
      out->is_proc = false;
      out->proc_params.clear();
      out->proc_return.reset();
      return true;
    case ExprKind::Unary: {
      if (expr.children.empty()) {
        if (error) *error = "unary missing operand";
        return false;
      }
      TypeRef operand;
      if (!InferExprType(expr.children[0], st, &operand, error)) return false;
      if (expr.op == "post?") {
        TaggedTypeInfo tagged;
        if (!ResolveTaggedType(operand, st, &tagged) || !tagged.value_type) {
          if (error) *error = "operator '?' requires optional or Result operand";
          return false;
        }
        return CloneTypeRef(*tagged.value_type, out);
      }
      if (expr.op == "await") {
        if (operand.name != "Promise" || operand.type_args.size() != 1 ||
            operand.pointer_depth != 0 || !operand.dims.empty()) {
          if (error) *error = "await requires Promise<T> operand";
          return false;
        }
        return CloneTypeRef(operand.type_args[0], out);
      }
      return CloneTypeRef(operand, out);
    }
    case ExprKind::Binary: {
      if (expr.children.size() < 2) {
        if (error) *error = "binary missing operands";
        return false;
      }
      TypeRef left;
      TypeRef right;
      if (!InferBinaryOperandTypes(expr, st, &left, &right, error)) return false;
      const bool is_bool_op =
          (expr.op == "==" || expr.op == "!=" || expr.op == "<" || expr.op == "<=" ||
           expr.op == ">" || expr.op == ">=" || expr.op == "&&" || expr.op == "||");
      TypeRef matched;
      if (left.name == right.name) {
        if (!CloneTypeRef(left, &matched)) return false;
      } else if (IsIntegerLiteralExpr(expr.children[0]) && TAST::IsIntegerScalarTypeName(right.name)) {
        if (!CloneTypeRef(right, &matched)) return false;
      } else if (IsIntegerLiteralExpr(expr.children[1]) && TAST::IsIntegerScalarTypeName(left.name)) {
        if (!CloneTypeRef(left, &matched)) return false;
      } else if (IsFloatLiteralExpr(expr.children[0]) && TAST::IsFloatTypeName(right.name)) {
        if (!CloneTypeRef(right, &matched)) return false;
      } else if (IsFloatLiteralExpr(expr.children[1]) && TAST::IsFloatTypeName(left.name)) {
        if (!CloneTypeRef(left, &matched)) return false;
      } else {
        if (error) {
          *error = "operand type mismatch for '" + expr.op + "': " +
                   GEN::TypeRefIdentity(left) + " and " + GEN::TypeRefIdentity(right);
        }
        return false;
      }
      if (is_bool_op) {
        out->name = "bool";
        return true;
      }
      return CloneTypeRef(matched, out);
    }
    case ExprKind::Switch:
      return InferSwitchExprType(expr, st, out, error);
    case ExprKind::Index: {
      if (expr.children.size() < 2) {
        if (error) *error = "index expression missing operands";
        return false;
      }
      TypeRef container;
      if (!InferExprType(expr.children[0], st, &container, error)) return false;
      if (container.name == "string" && container.dims.empty()) {
        out->name = "char";
        return true;
      }
      if (container.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays, lists, and strings";
        return false;
      }
      if (!CloneElementType(container, out)) {
        if (error) *error = "failed to determine index element type";
        return false;
      }
      return true;
    }
    case ExprKind::ArtifactLiteral:
      if (error) *error = "artifact literal requires expected type";
      return false;
    case ExprKind::Member: {
      if (expr.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = expr.children[0];
      const bool is_ptr = (expr.op == "->");
      std::string module_name;
      LibraryModuleId module_id{};
      if (GetModuleNameFromExpr(base, &module_name) && ResolveReservedModuleId(st, module_name, &module_id)) {
        Simple::Lang::AST::TypeRef reserved_var_type;
        if (RAST::GetReservedModuleVarType(std::string(ToCanonicalName(module_id)), expr.text, &reserved_var_type)) {
          if (!CloneTypeRef(reserved_var_type, out)) return false;
          return true;
        }
      }
      if (base.kind == ExprKind::Identifier) {
        LibraryModuleId resolved_id{};
        if (ResolveReservedModuleId(st, base.text, &resolved_id) &&
            IsLibraryModule(resolved_id, SystemModule::FFI) &&
            ParseMember(SystemModule::FFI, expr.text) == SystemMember(SystemFFIMember::Supported)) {
          out->name = "bool";
          return true;
        }
        if (ResolveReservedModuleId(st, base.text, &resolved_id) &&
            IsLibraryModule(resolved_id, SystemModule::OS) &&
            (expr.text == "is_linux" || expr.text == "is_macos" ||
             expr.text == "is_windows" || expr.text == "has_dl")) {
          out->name = "bool";
          return true;
        }
        auto enum_it = st.enum_values.find(base.text);
        if (enum_it != st.enum_values.end()) {
          out->name = base.text;
          return true;
        }
        const std::string key = base.text + "." + expr.text;
        auto gtype_it = st.global_types.find(key);
        if (gtype_it != st.global_types.end()) {
          return CloneTypeRef(gtype_it->second, out);
        }
      }
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      if (is_ptr) {
        if (base_type.pointer_depth == 0) {
          if (error) *error = "pointer member access requires a pointer type";
          return false;
        }
        base_type.pointer_depth -= 1;
      }
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      const auto& layout = layout_it->second;
      auto field_it = layout.field_index.find(expr.text);
      if (field_it == layout.field_index.end()) {
        if (error) *error = "unknown field '" + expr.text + "'";
        return false;
      }
      return CloneTypeRef(layout.fields[field_it->second].type, out);
    }
    case ExprKind::Call: {
      if (expr.children.empty()) {
        if (error) *error = "call missing callee";
        return false;
      }
      const Expr& callee = expr.children[0];
      if (callee.kind == ExprKind::Identifier) {
        if (callee.text == "len") {
          out->name = "i32";
          return true;
        }
        std::string cast_target;
        if (GetAtCastTargetName(callee.text, &cast_target)) {
          out->name = cast_target;
          return true;
        }
        auto it = st.func_returns.find(callee.text);
        if (it != st.func_returns.end()) {
          return CloneCallReturn(st, callee.text, it->second, out);
        }
        auto ext_it = st.extern_returns.find(callee.text);
        if (ext_it != st.extern_returns.end()) {
          return CloneTypeRef(ext_it->second, out);
        }
        auto local_it = st.local_types.find(callee.text);
        if (local_it != st.local_types.end() && local_it->second.is_proc) {
          if (local_it->second.proc_return) return CloneTypeRef(*local_it->second.proc_return, out);
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        auto upvalue_it = st.current_upvalues.find(callee.text);
        if (upvalue_it != st.current_upvalues.end() && upvalue_it->second.type.is_proc) {
          if (upvalue_it->second.type.proc_return) {
            return CloneTypeRef(*upvalue_it->second.type.proc_return, out);
          }
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        auto global_it = st.global_types.find(callee.text);
        if (global_it != st.global_types.end() && global_it->second.is_proc) {
          if (global_it->second.proc_return) return CloneTypeRef(*global_it->second.proc_return, out);
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        std::string using_module;
        if (ResolveUsingReservedMember(st, callee.text, &using_module)) {
          if (IsCanonicalLibraryModule(using_module, StandardModule::IO) && IsIoPrintName(callee.text)) {
            out->name = "void";
            out->type_args.clear();
            out->dims.clear();
            out->is_proc = false;
            out->proc_params.clear();
            out->proc_return.reset();
            return true;
          }
          if (IsCanonicalLibraryModule(using_module, StandardModule::Math) &&
              ParseMember(StandardModule::Math, callee.text) &&
              !expr.args.empty()) {
            if (!InferExprType(expr.args[0], st, out, nullptr)) return false;
            return true;
          }
          if ((IsCanonicalLibraryModule(using_module, SystemModule::Time) ||
               IsCanonicalLibraryModule(using_module, StandardModule::Time)) &&
              (callee.text == "mono_ns" || callee.text == "wall_ns")) {
            out->name = "i64";
            out->type_args.clear();
            out->dims.clear();
            out->is_proc = false;
            out->proc_params.clear();
            out->proc_return.reset();
            return true;
          }
          if (IsNativeReservedModule(using_module)) {
            auto ret_mod_it = st.extern_returns_by_module.find(using_module);
            if (ret_mod_it == st.extern_returns_by_module.end()) return false;
            auto ret_it = ret_mod_it->second.find(callee.text);
            if (ret_it == ret_mod_it->second.end()) return false;
            return CloneTypeRef(ret_it->second, out);
          }
        }
        if (ResolveUsingModuleExternMember(st, callee.text, &using_module)) {
          auto ret_mod_it = st.extern_returns_by_module.find(using_module);
          if (ret_mod_it == st.extern_returns_by_module.end()) return false;
          auto ret_it = ret_mod_it->second.find(callee.text);
          if (ret_it == ret_mod_it->second.end()) return false;
          return CloneTypeRef(ret_it->second, out);
        }
      }
      if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
        const Expr& base = callee.children[0];
        if (IsIoPrintCallExpr(callee, st)) {
          out->name = "void";
          out->type_args.clear();
          out->dims.clear();
          out->is_proc = false;
          out->proc_params.clear();
          out->proc_return.reset();
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          if (ResolveDlModuleForIdentifier(base.text, st, &dl_module)) {
            auto ext_mod_it = st.extern_returns_by_module.find(dl_module);
            if (ext_mod_it != st.extern_returns_by_module.end()) {
              auto ext_it = ext_mod_it->second.find(callee.text);
              if (ext_it != ext_mod_it->second.end()) {
                return CloneTypeRef(ext_it->second, out);
              }
            }
          }
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name)) {
          LibraryModuleId reserved_module_id{};
          const bool has_reserved_module =
              ResolveReservedModuleId(st, module_name, &reserved_module_id);
          std::string reserved_module;
          if (has_reserved_module) reserved_module = std::string(ToCanonicalName(reserved_module_id));
          if (has_reserved_module) {
            const bool reserved_is_ffi = IsLibraryModule(reserved_module_id, SystemModule::FFI);
            const std::string member_name =
                reserved_is_ffi ? NormalizeCoreDlMember(callee.text) : callee.text;
            if (IsLibraryModule(reserved_module_id, StandardModule::Math) &&
                ParseMember(StandardModule::Math, member_name) &&
                !expr.args.empty()) {
              if (!InferExprType(expr.args[0], st, out, nullptr)) return false;
              return true;
            }
            if ((IsLibraryModule(reserved_module_id, SystemModule::Time) ||
                 IsLibraryModule(reserved_module_id, StandardModule::Time)) &&
                (member_name == "mono_ns" || member_name == "wall_ns")) {
              out->name = "i64";
              out->type_args.clear();
              out->dims.clear();
              out->is_proc = false;
              out->proc_params.clear();
              out->proc_return.reset();
              return true;
            }
          }
          auto ext_mod_it = st.extern_returns_by_module.find(module_name);
          std::string ext_module_name = module_name;
          const bool ext_is_system_ffi =
              (ParseCanonicalLibraryModule(module_name) &&
               IsCanonicalLibraryModule(module_name, SystemModule::FFI)) ||
              (has_reserved_module && IsLibraryModule(reserved_module_id, SystemModule::FFI));
          if (ext_mod_it == st.extern_returns_by_module.end() && has_reserved_module) {
            ext_mod_it = st.extern_returns_by_module.find(reserved_module);
            if (ext_mod_it != st.extern_returns_by_module.end()) {
              ext_module_name = reserved_module;
            }
          }
          if (ext_mod_it != st.extern_returns_by_module.end()) {
            const std::string member_name =
                ext_is_system_ffi ? NormalizeCoreDlMember(callee.text) : callee.text;
            auto ext_it = ext_mod_it->second.find(member_name);
            if (ext_it != ext_mod_it->second.end()) {
              return CloneTypeRef(ext_it->second, out);
            }
          }
          LibraryModuleId resolved_module_id{};
          const bool module_is_reserved = ResolveReservedModuleId(st, module_name, &resolved_module_id);
          const bool module_is_system_ffi =
              (ParseCanonicalLibraryModule(module_name) &&
               IsCanonicalLibraryModule(module_name, SystemModule::FFI)) ||
              (module_is_reserved && IsLibraryModule(resolved_module_id, SystemModule::FFI));
          const std::string member_name =
              module_is_system_ffi ? NormalizeCoreDlMember(callee.text) : callee.text;
          const std::string key = module_name + "." + member_name;
          auto module_it = st.module_func_names.find(key);
          if (module_it != st.module_func_names.end()) {
            auto ret_it = st.func_returns.find(module_it->second);
            if (ret_it != st.func_returns.end()) {
              return CloneCallReturn(st, module_it->second, ret_it->second, out);
            }
          }
        }
        TypeRef base_type;
        if (InferExprType(base, st, &base_type, nullptr)) {
          if (base_type.name == "Promise" && base_type.type_args.size() == 1 &&
              base_type.dims.empty() &&
              (callee.text == "cancel" || callee.text == "isDone" ||
               callee.text == "isCancelled")) {
            out->name = "bool";
            return true;
          }
          if (!base_type.dims.empty() && base_type.dims.front().is_list) {
            TypeRef element_type;
            if (!CloneElementType(base_type, &element_type)) return false;
            if (callee.text == "len") {
              out->name = "i32";
              out->type_args.clear();
              out->dims.clear();
              out->is_proc = false;
              out->proc_params.clear();
              out->proc_return.reset();
              return true;
            }
            if (callee.text == "push" || callee.text == "insert" || callee.text == "clear") {
              out->name = "void";
              out->type_args.clear();
              out->dims.clear();
              out->is_proc = false;
              out->proc_params.clear();
              out->proc_return.reset();
              return true;
            }
            if (callee.text == "pop" || callee.text == "remove") {
              return CloneTypeRef(element_type, out);
            }
          }
          const std::string key = base_type.name + "." + callee.text;
          auto method_it = st.artifact_method_names.find(key);
          if (method_it != st.artifact_method_names.end()) {
            auto ret_it = st.func_returns.find(method_it->second);
            if (ret_it != st.func_returns.end()) {
              return CloneCallReturn(st, method_it->second, ret_it->second, out);
            }
          }
        }
      }
      TypeRef callable_type;
      if (InferExprType(callee, st, &callable_type, nullptr) && callable_type.is_proc &&
          callable_type.proc_return) {
        return CloneTypeRef(*callable_type.proc_return, out);
      }
      if (error) {
        *error = "call type not supported in SIR emission";
        if (!callee.text.empty()) *error += ": " + callee.text;
      }
      return false;
    }
    default:
      if (error) *error = "expression not supported for SIR emission";
      return false;
  }
}

bool InferBinaryOperandTypes(const Expr& expr,
                             const EmitState& st,
                             TypeRef* left,
                             TypeRef* right,
                             std::string* error) {
  const bool have_left = InferExprType(expr.children[0], st, left, nullptr);
  const bool have_right = InferExprType(expr.children[1], st, right, nullptr);
  if (!have_left && have_right && IsDirectFnLiteralCall(expr.children[0])) {
    if (!CloneTypeRef(*right, left)) return false;
  } else if (!have_left) {
    return InferExprType(expr.children[0], st, left, error);
  }
  if (!have_right && have_left && IsDirectFnLiteralCall(expr.children[1])) {
    if (!CloneTypeRef(*left, right)) return false;
  } else if (!have_right) {
    return InferExprType(expr.children[1], st, right, error);
  }
  return true;
}

bool EmitConstForType(EmitState& st,
                      const TypeRef& type,
                      const Expr& expr,
                      std::string* error) {
  if (expr.literal_kind == LiteralKind::String) {
    std::string name;
    if (!AddStringConst(st, expr.text, &name)) return false;
    (*st.out) << "  const string " << name << "\n";
    return PushStack(st, 1);
  }
  if (expr.literal_kind == LiteralKind::Char) {
    uint16_t value = static_cast<unsigned char>(expr.text.empty() ? '\0' : expr.text[0]);
    (*st.out) << "  const char " << value << "\n";
    return PushStack(st, 1);
  }
  if (expr.literal_kind == LiteralKind::Bool) {
    const std::string text = expr.text;
    uint32_t value = (text == "true") ? 1u : 0u;
    (*st.out) << "  const bool " << value << "\n";
    return PushStack(st, 1);
  }

  if (!TAST::IsNumericScalarTypeName(type.name)) {
    if (error) *error = "literal type not supported for SIR emission";
    return false;
  }

  if (expr.literal_kind == LiteralKind::Float) {
    (*st.out) << "  const " << type.name << " " << expr.text << "\n";
    return PushStack(st, 1);
  }

  (*st.out) << "  const " << type.name << " " << expr.text << "\n";
  return PushStack(st, 1);
}

bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error);

bool EmitStmt(EmitState& st, const Stmt& stmt, std::string* error);

bool EmitIndexSetOp(EmitState& st,
                    const TypeRef& container_type,
                    const char* op_suffix) {
  if (container_type.dims.front().is_list) {
    (*st.out) << "  list.set " << op_suffix << "\n";
  } else {
    (*st.out) << "  array.set " << op_suffix << "\n";
  }
  PopStack(st, 3);
  return true;
}

bool EmitIndexGetOp(EmitState& st,
                    const TypeRef& container_type,
                    const char* op_suffix) {
  if (container_type.dims.front().is_list) {
    (*st.out) << "  list.get " << op_suffix << "\n";
  } else {
    (*st.out) << "  array.get " << op_suffix << "\n";
  }
  PopStack(st, 2);
  return PushStack(st, 1);
}

const char* AssignOpToBinaryOp(const std::string& op) {
  if (op == "+=") return "+";
  if (op == "-=") return "-";
  if (op == "*=") return "*";
  if (op == "/=") return "/";
  if (op == "%=") return "%";
  if (op == "&=") return "&";
  if (op == "|=") return "|";
  if (op == "^=") return "^";
  if (op == "<<=") return "<<";
  if (op == ">>=") return ">>";
  return nullptr;
}

bool EmitCaptureCellRef(EmitState& st,
                        const std::string& name,
                        std::string* error) {
  auto local_it = st.local_indices.find(name);
  if (local_it != st.local_indices.end() && st.captured_locals.find(name) != st.captured_locals.end()) {
    (*st.out) << "  ldloc " << local_it->second << "\n";
    return PushStack(st, 1);
  }
  auto upvalue_it = st.current_upvalues.find(name);
  if (upvalue_it != st.current_upvalues.end()) {
    (*st.out) << "  ldupv " << name << "\n";
    return PushStack(st, 1);
  }
  if (error) *error = "unknown captured binding '" + name + "'";
  return false;
}

bool EmitCaptureCellLoad(EmitState& st,
                         const std::string& name,
                         const TypeRef& type,
                         std::string* error) {
  if (!EmitCaptureCellRef(st, name, error)) return false;
  const char* suffix = VmOpSuffixForType(type, st);
  if (!suffix) {
    if (error) *error = "unsupported captured binding type for '" + name + "'";
    return false;
  }
  (*st.out) << "  const i32 0\n";
  PushStack(st, 1);
  (*st.out) << "  list.get " << suffix << "\n";
  PopStack(st, 2);
  return PushStack(st, 1);
}

bool EmitCaptureCellCreate(EmitState& st,
                           const std::string& name,
                           uint16_t local_index,
                           const TypeRef& type,
                           const Expr* initializer,
                           std::string* error) {
  const char* type_name = VmTypeNameForElement(type, st);
  const char* suffix = VmOpSuffixForType(type, st);
  if (!type_name || !suffix) {
    if (error) *error = "unsupported captured binding type for '" + name + "'";
    return false;
  }
  (*st.out) << "  newlist " << type_name << " 1\n";
  PushStack(st, 1);
  (*st.out) << "  dup\n";
  PushStack(st, 1);
  if (initializer) {
    if (!EmitExpr(st, *initializer, &type, error)) return false;
  } else if (!EmitDefaultInit(st, type, error)) {
    return false;
  }
  (*st.out) << "  list.push " << suffix << "\n";
  PopStack(st, 2);
  (*st.out) << "  stloc " << local_index << "\n";
  return PopStack(st, 1);
}

bool EmitCaptureCellBoxExistingLocal(EmitState& st,
                                     const std::string& name,
                                     uint16_t source_index,
                                     uint16_t cell_index,
                                     const TypeRef& type,
                                     std::string* error) {
  const char* type_name = VmTypeNameForElement(type, st);
  const char* suffix = VmOpSuffixForType(type, st);
  if (!type_name || !suffix) {
    if (error) *error = "unsupported captured parameter type for '" + name + "'";
    return false;
  }
  (*st.out) << "  newlist " << type_name << " 1\n";
  PushStack(st, 1);
  (*st.out) << "  dup\n";
  PushStack(st, 1);
  (*st.out) << "  ldloc " << source_index << "\n";
  PushStack(st, 1);
  (*st.out) << "  list.push " << suffix << "\n";
  PopStack(st, 2);
  (*st.out) << "  stloc " << cell_index << "\n";
  return PopStack(st, 1);
}

bool EmitCapturedAssignment(EmitState& st,
                            const std::string& name,
                            const TypeRef& type,
                            const Expr& value,
                            const std::string& op,
                            bool return_value,
                            std::string* error) {
  const char* suffix = VmOpSuffixForType(type, st);
  if (!suffix) {
    if (error) *error = "unsupported captured assignment type for '" + name + "'";
    return false;
  }
  std::string rhs_temp_name;
  uint16_t rhs_temp_index = 0;
  if (!AllocateTempLocal(st, type, &rhs_temp_name, &rhs_temp_index, error)) return false;
  if (!EmitExpr(st, value, &type, error)) return false;
  (*st.out) << "  stloc " << rhs_temp_index << "\n";
  PopStack(st, 1);
  if (!EmitCaptureCellRef(st, name, error)) return false;
  if (op == "=") {
    (*st.out) << "  const i32 0\n";
    PushStack(st, 1);
    (*st.out) << "  ldloc " << rhs_temp_index << "\n";
    PushStack(st, 1);
  } else {
    (*st.out) << "  dup\n";
    PushStack(st, 1);
    (*st.out) << "  const i32 0\n";
    PushStack(st, 1);
    (*st.out) << "  list.get " << suffix << "\n";
    PopStack(st, 2);
    PushStack(st, 1);
    (*st.out) << "  ldloc " << rhs_temp_index << "\n";
    PushStack(st, 1);
    const char* bin_op = AssignOpToBinaryOp(op);
    const char* op_type = bin_op && (std::string(bin_op) == "&" || std::string(bin_op) == "|" ||
                                     std::string(bin_op) == "^" || std::string(bin_op) == "<<" ||
                                     std::string(bin_op) == ">>")
                              ? NormalizeBitwiseOpType(type.name)
                              : NormalizeNumericOpType(type.name);
    if (!bin_op || !op_type) {
      if (error) *error = "unsupported captured assignment operator '" + op + "'";
      return false;
    }
    PopStack(st, 1);
    if (std::string(bin_op) == "+") (*st.out) << "  add " << op_type << "\n";
    else if (std::string(bin_op) == "-") (*st.out) << "  sub " << op_type << "\n";
    else if (std::string(bin_op) == "*") (*st.out) << "  mul " << op_type << "\n";
    else if (std::string(bin_op) == "/") (*st.out) << "  div " << op_type << "\n";
    else if (std::string(bin_op) == "%") (*st.out) << "  mod " << op_type << "\n";
    else if (std::string(bin_op) == "&") (*st.out) << "  and " << op_type << "\n";
    else if (std::string(bin_op) == "|") (*st.out) << "  or " << op_type << "\n";
    else if (std::string(bin_op) == "^") (*st.out) << "  xor " << op_type << "\n";
    else if (std::string(bin_op) == "<<") (*st.out) << "  shl " << op_type << "\n";
    else if (std::string(bin_op) == ">>") (*st.out) << "  shr " << op_type << "\n";
    (*st.out) << "  const i32 0\n";
    PushStack(st, 1);
    (*st.out) << "  swap\n";
  }
  (*st.out) << "  list.set " << suffix << "\n";
  PopStack(st, 3);
  if (return_value) return EmitCaptureCellLoad(st, name, type, error);
  return true;
}

bool EmitLocalAssignment(EmitState& st,
                         const std::string& name,
                         const TypeRef& type,
                         const Expr& value,
                         const std::string& op,
                         bool return_value,
                         std::string* error) {
  if (st.captured_locals.find(name) != st.captured_locals.end() ||
      st.current_upvalues.find(name) != st.current_upvalues.end()) {
    return EmitCapturedAssignment(st, name, type, value, op, return_value, error);
  }
  auto it = st.local_indices.find(name);
  if (it == st.local_indices.end()) {
    if (error) *error = "unknown local '" + name + "'";
    return false;
  }
  if (op == "=") {
    if (!EmitExpr(st, value, &type, error)) return false;
    (*st.out) << "  stloc " << it->second << "\n";
    PopStack(st, 1);
    if (return_value) {
      (*st.out) << "  ldloc " << it->second << "\n";
      PushStack(st, 1);
    }
    return true;
  }

  const char* bin_op = AssignOpToBinaryOp(op);
  if (!bin_op) {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  std::string rhs_temp_name;
  uint16_t rhs_temp_index = 0;
  if (!AllocateTempLocal(st, type, &rhs_temp_name, &rhs_temp_index, error)) return false;
  if (!EmitExpr(st, value, &type, error)) return false;
  (*st.out) << "  stloc " << rhs_temp_index << "\n";
  PopStack(st, 1);
  (*st.out) << "  ldloc " << it->second << "\n";
  PushStack(st, 1);
  (*st.out) << "  ldloc " << rhs_temp_index << "\n";
  PushStack(st, 1);
  PopStack(st, 1);
  const char* op_type = nullptr;
  if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
      std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
    op_type = NormalizeBitwiseOpType(type.name);
  } else {
    op_type = NormalizeNumericOpType(type.name);
  }
  if (!op_type) {
    if (error) *error = "unsupported operand type for '" + op + "'";
    return false;
  }
  if (std::string(bin_op) == "+") {
    (*st.out) << "  add " << op_type << "\n";
  } else if (std::string(bin_op) == "-") {
    (*st.out) << "  sub " << op_type << "\n";
  } else if (std::string(bin_op) == "*") {
    (*st.out) << "  mul " << op_type << "\n";
  } else if (std::string(bin_op) == "/") {
    (*st.out) << "  div " << op_type << "\n";
  } else if (std::string(bin_op) == "%" && TAST::IsIntegerScalarTypeName(type.name)) {
    (*st.out) << "  mod " << op_type << "\n";
  } else if (std::string(bin_op) == "&") {
    (*st.out) << "  and " << op_type << "\n";
  } else if (std::string(bin_op) == "|") {
    (*st.out) << "  or " << op_type << "\n";
  } else if (std::string(bin_op) == "^") {
    (*st.out) << "  xor " << op_type << "\n";
  } else if (std::string(bin_op) == "<<") {
    (*st.out) << "  shl " << op_type << "\n";
  } else if (std::string(bin_op) == ">>") {
    (*st.out) << "  shr " << op_type << "\n";
  } else {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  (*st.out) << "  stloc " << it->second << "\n";
  PopStack(st, 1);
  if (return_value) {
    (*st.out) << "  ldloc " << it->second << "\n";
    PushStack(st, 1);
  }
  return true;
}

bool EmitGlobalAssignment(EmitState& st,
                          const std::string& name,
                          const TypeRef& type,
                          const Expr& value,
                          const std::string& op,
                          bool return_value,
                          std::string* error) {
  auto it = st.global_indices.find(name);
  if (it == st.global_indices.end()) {
    if (error) *error = "unknown global '" + name + "'";
    return false;
  }
  if (op == "=") {
    if (!EmitExpr(st, value, &type, error)) return false;
    (*st.out) << "  stglob " << it->second << "\n";
    PopStack(st, 1);
    if (return_value) {
      (*st.out) << "  ldglob " << it->second << "\n";
      PushStack(st, 1);
    }
    return true;
  }

  const char* bin_op = AssignOpToBinaryOp(op);
  if (!bin_op) {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  (*st.out) << "  ldglob " << it->second << "\n";
  PushStack(st, 1);
  if (!EmitExpr(st, value, &type, error)) return false;
  PopStack(st, 1);
  const char* op_type = nullptr;
  if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
      std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
    op_type = NormalizeBitwiseOpType(type.name);
  } else {
    op_type = NormalizeNumericOpType(type.name);
  }
  if (!op_type) {
    if (error) *error = "unsupported operand type for '" + op + "'";
    return false;
  }
  if (std::string(bin_op) == "+") {
    (*st.out) << "  add " << op_type << "\n";
  } else if (std::string(bin_op) == "-") {
    (*st.out) << "  sub " << op_type << "\n";
  } else if (std::string(bin_op) == "*") {
    (*st.out) << "  mul " << op_type << "\n";
  } else if (std::string(bin_op) == "/") {
    (*st.out) << "  div " << op_type << "\n";
  } else if (std::string(bin_op) == "%" && TAST::IsIntegerScalarTypeName(type.name)) {
    (*st.out) << "  mod " << op_type << "\n";
  } else if (std::string(bin_op) == "&") {
    (*st.out) << "  and " << op_type << "\n";
  } else if (std::string(bin_op) == "|") {
    (*st.out) << "  or " << op_type << "\n";
  } else if (std::string(bin_op) == "^") {
    (*st.out) << "  xor " << op_type << "\n";
  } else if (std::string(bin_op) == "<<") {
    (*st.out) << "  shl " << op_type << "\n";
  } else if (std::string(bin_op) == ">>") {
    (*st.out) << "  shr " << op_type << "\n";
  } else {
    if (error) *error = "unsupported assignment operator '" + op + "'";
    return false;
  }
  (*st.out) << "  stglob " << it->second << "\n";
  PopStack(st, 1);
  if (return_value) {
    (*st.out) << "  ldglob " << it->second << "\n";
    PushStack(st, 1);
  }
  return true;
}

bool EmitAssignmentExpr(EmitState& st, const Expr& expr, std::string* error) {
  if (expr.children.size() != 2) {
    if (error) *error = "assignment missing operands";
    return false;
  }
  const Expr& target = expr.children[0];
  if (target.kind == ExprKind::Identifier) {
    auto type_it = st.local_types.find(target.text);
    if (type_it != st.local_types.end()) {
      return EmitLocalAssignment(st, target.text, type_it->second, expr.children[1], expr.op, true, error);
    }
    auto upvalue_it = st.current_upvalues.find(target.text);
    if (upvalue_it != st.current_upvalues.end()) {
      return EmitCapturedAssignment(
          st, target.text, upvalue_it->second.type, expr.children[1], expr.op, true, error);
    }
    auto gtype_it = st.global_types.find(target.text);
    if (gtype_it != st.global_types.end()) {
      return EmitGlobalAssignment(st, target.text, gtype_it->second, expr.children[1], expr.op, true, error);
    }
    if (error) *error = "unknown type for local '" + target.text + "'";
    return false;
  }
  if (target.kind == ExprKind::Index) {
    if (target.children.size() != 2) {
      if (error) *error = "index assignment expects target and index";
      return false;
    }
    TypeRef container_type;
    if (!InferExprType(target.children[0], st, &container_type, error)) return false;
    if (container_type.dims.empty()) {
      if (error) *error = "index assignment expects array or list target";
      return false;
    }
    TypeRef element_type;
    if (!CloneElementType(container_type, &element_type)) {
      if (error) *error = "failed to resolve index element type";
      return false;
    }
    const char* op_suffix = VmOpSuffixForType(element_type, st);
    if (!op_suffix) {
      if (error) *error = "unsupported index assignment element type for SIR emission";
      return false;
    }
    if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
    TypeRef index_type;
    index_type.name = "i32";
    if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
    if (expr.op != "=") {
      if (!EmitDup2(st)) return false;
      if (!EmitIndexGetOp(st, container_type, op_suffix)) return false;
      if (!EmitExpr(st, expr.children[1], &element_type, error)) return false;
      PopStack(st, 1);
      const char* bin_op = AssignOpToBinaryOp(expr.op);
      if (!bin_op) {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      const char* op_type = nullptr;
      if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
          std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
        op_type = NormalizeBitwiseOpType(element_type.name);
      } else {
        op_type = NormalizeNumericOpType(element_type.name);
      }
      if (!op_type) {
        if (error) *error = "unsupported operand type for '" + expr.op + "'";
        return false;
      }
      if (std::string(bin_op) == "+") {
        (*st.out) << "  add " << op_type << "\n";
      } else if (std::string(bin_op) == "-") {
        (*st.out) << "  sub " << op_type << "\n";
      } else if (std::string(bin_op) == "*") {
        (*st.out) << "  mul " << op_type << "\n";
      } else if (std::string(bin_op) == "/") {
        (*st.out) << "  div " << op_type << "\n";
      } else if (std::string(bin_op) == "%" && TAST::IsIntegerScalarTypeName(element_type.name)) {
        (*st.out) << "  mod " << op_type << "\n";
      } else if (std::string(bin_op) == "&") {
        (*st.out) << "  and " << op_type << "\n";
      } else if (std::string(bin_op) == "|") {
        (*st.out) << "  or " << op_type << "\n";
      } else if (std::string(bin_op) == "^") {
        (*st.out) << "  xor " << op_type << "\n";
      } else if (std::string(bin_op) == "<<") {
        (*st.out) << "  shl " << op_type << "\n";
      } else if (std::string(bin_op) == ">>") {
        (*st.out) << "  shr " << op_type << "\n";
      } else {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      if (!EmitDup(st)) return false;
      if (!EmitIndexSetOp(st, container_type, op_suffix)) return false;
      return true;
    }
    if (!EmitExpr(st, expr.children[1], &element_type, error)) return false;
    if (!EmitDup(st)) return false;
    if (!EmitIndexSetOp(st, container_type, op_suffix)) return false;
    return true;
  }
  if (target.kind == ExprKind::Member) {
    if (target.children.empty()) {
      if (error) *error = "member assignment missing base";
      return false;
    }
    const Expr& base = target.children[0];
    const bool is_ptr = (target.op == "->");
    if (base.kind == ExprKind::Identifier) {
      const std::string qualified = base.text + "." + target.text;
      auto gtype_it = st.global_types.find(qualified);
      if (gtype_it != st.global_types.end()) {
        return EmitGlobalAssignment(st, qualified, gtype_it->second, expr.children[1], expr.op, true, error);
      }
    }
    TypeRef base_type;
    if (!InferExprType(base, st, &base_type, error)) return false;
    if (is_ptr) {
      if (base_type.pointer_depth == 0) {
        if (error) *error = "pointer member assignment requires a pointer type";
        return false;
      }
      base_type.pointer_depth -= 1;
    }
    auto layout_it = st.artifact_layouts.find(base_type.name);
    if (layout_it == st.artifact_layouts.end()) {
      if (error) *error = "member assignment base is not an artifact";
      return false;
    }
    auto field_it = layout_it->second.field_index.find(target.text);
    if (field_it == layout_it->second.field_index.end()) {
      if (error) *error = "unknown field '" + target.text + "'";
      return false;
    }
    const TypeRef& field_type = layout_it->second.fields[field_it->second].type;
    if (!EmitExpr(st, base, &base_type, error)) return false;
    if (expr.op != "=") {
      if (!EmitDup(st)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << target.text << "\n";
      if (!EmitExpr(st, expr.children[1], &field_type, error)) return false;
      PopStack(st, 1);
      const char* bin_op = AssignOpToBinaryOp(expr.op);
      if (!bin_op) {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      const char* op_type = nullptr;
      if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
          std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
        op_type = NormalizeBitwiseOpType(field_type.name);
      } else {
        op_type = NormalizeNumericOpType(field_type.name);
      }
      if (!op_type) {
        if (error) *error = "unsupported operand type for '" + expr.op + "'";
        return false;
      }
      if (std::string(bin_op) == "+") {
        (*st.out) << "  add " << op_type << "\n";
      } else if (std::string(bin_op) == "-") {
        (*st.out) << "  sub " << op_type << "\n";
      } else if (std::string(bin_op) == "*") {
        (*st.out) << "  mul " << op_type << "\n";
      } else if (std::string(bin_op) == "/") {
        (*st.out) << "  div " << op_type << "\n";
      } else if (std::string(bin_op) == "%" && TAST::IsIntegerScalarTypeName(field_type.name)) {
        (*st.out) << "  mod " << op_type << "\n";
      } else if (std::string(bin_op) == "&") {
        (*st.out) << "  and " << op_type << "\n";
      } else if (std::string(bin_op) == "|") {
        (*st.out) << "  or " << op_type << "\n";
      } else if (std::string(bin_op) == "^") {
        (*st.out) << "  xor " << op_type << "\n";
      } else if (std::string(bin_op) == "<<") {
        (*st.out) << "  shl " << op_type << "\n";
      } else if (std::string(bin_op) == ">>") {
        (*st.out) << "  shr " << op_type << "\n";
      } else {
        if (error) *error = "unsupported assignment operator '" + expr.op + "'";
        return false;
      }
      if (!EmitDup(st)) return false;
      (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
      PopStack(st, 2);
      return true;
    }
    if (!EmitExpr(st, expr.children[1], &field_type, error)) return false;
    if (!EmitDup(st)) return false;
    (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
    PopStack(st, 2);
    return true;
  }
  if (error) *error = "assignment target not supported in SIR emission";
  return false;
}

bool EmitUnary(EmitState& st,
               const Expr& expr,
               const TypeRef* expected,
               std::string* error) {
  if (expr.children.empty()) {
    if (error) *error = "unary missing operand";
    return false;
  }
  TypeRef operand_type;
  if (!InferExprType(expr.children[0], st, &operand_type, error)) return false;
  if (expr.op == "await") {
    if (operand_type.name != "Promise" || operand_type.type_args.size() != 1 ||
        operand_type.pointer_depth != 0 || !operand_type.dims.empty()) {
      if (error) *error = "await requires Promise<T> operand";
      return false;
    }
    const bool returns_void = operand_type.type_args[0].name == "void";
    const char* result_type =
        returns_void ? "void" : VmTypeNameForElement(operand_type.type_args[0], st);
    if (!result_type) {
      if (error) *error = "await result type is unsupported";
      return false;
    }
    if (!EmitExpr(st, expr.children[0], &operand_type, error)) return false;
    (*st.out) << "  await " << result_type << "\n";
    if (returns_void) PopStack(st, 1);
    return true;
  }
  if (expr.op == "post?") {
    TaggedTypeInfo operand_tagged;
    if (!ResolveTaggedType(operand_type, st, &operand_tagged) || !operand_tagged.value_type) {
      if (error) *error = "operator '?' requires optional or Result operand";
      return false;
    }
    auto return_it = st.func_returns.find(st.current_func);
    if (return_it == st.func_returns.end()) {
      if (error) *error = "operator '?' requires an enclosing function";
      return false;
    }
    TaggedTypeInfo return_tagged;
    if (!ResolveTaggedType(return_it->second, st, &return_tagged) ||
        return_tagged.kind != operand_tagged.kind) {
      if (error) *error = "operator '?' return type is incompatible with operand";
      return false;
    }

    std::string temp_name;
    uint16_t temp_index = 0;
    if (!AllocateTempLocal(st, operand_type, &temp_name, &temp_index, error)) return false;
    if (!EmitExpr(st, expr.children[0], &operand_type, error)) return false;
    (*st.out) << "  stloc " << temp_index << "\n";
    PopStack(st, 1);
    const std::string value_label = NewLabel(st, "propagate_value_");

    if (operand_tagged.kind == TaggedArtifactKind::Optional) {
      (*st.out) << "  ldloc " << temp_index << "\n";
      PushStack(st, 1);
      (*st.out) << "  isnull\n";
      (*st.out) << "  jmp.false " << value_label << "\n";
      PopStack(st, 1);
      (*st.out) << "  const null\n";
      PushStack(st, 1);
      (*st.out) << "  ret\n";
      st.stack_cur = 0;
      (*st.out) << value_label << ":\n";
      (*st.out) << "  ldloc " << temp_index << "\n";
      PushStack(st, 1);
      (*st.out) << "  ldfld " << operand_type.name << ".value\n";
      return true;
    }

    (*st.out) << "  ldloc " << temp_index << "\n";
    PushStack(st, 1);
    (*st.out) << "  ldfld " << operand_type.name << ".tag\n";
    (*st.out) << "  const i32 0\n";
    PushStack(st, 1);
    (*st.out) << "  cmp.eq i32\n";
    PopStack(st, 1);
    (*st.out) << "  jmp.true " << value_label << "\n";
    PopStack(st, 1);

    const auto return_layout_it = st.artifact_layouts.find(return_it->second.name);
    if (return_layout_it == st.artifact_layouts.end()) {
      if (error) *error = "missing Result return layout for propagation";
      return false;
    }
    (*st.out) << "  newobj " << return_it->second.name << "\n";
    PushStack(st, 1);
    for (const auto& field : return_layout_it->second.fields) {
      (*st.out) << "  dup\n";
      PushStack(st, 1);
      if (field.name == "tag") {
        (*st.out) << "  const i32 1\n";
        PushStack(st, 1);
      } else if (field.name == "error") {
        (*st.out) << "  ldloc " << temp_index << "\n";
        PushStack(st, 1);
        (*st.out) << "  ldfld " << operand_type.name << ".error\n";
      } else if (!EmitInactivePayload(st, field.type, error)) {
        return false;
      }
      (*st.out) << "  stfld " << return_it->second.name << "." << field.name << "\n";
      PopStack(st, 2);
    }
    (*st.out) << "  ret\n";
    st.stack_cur = 0;
    (*st.out) << value_label << ":\n";
    (*st.out) << "  ldloc " << temp_index << "\n";
    PushStack(st, 1);
    (*st.out) << "  ldfld " << operand_type.name << ".value\n";
    return true;
  }
  const TypeRef* use_type = expected ? expected : &operand_type;
  if (expr.op == "++" || expr.op == "--") {
    const char* op_name = expr.op == "++" ? IncOpForType(use_type->name) : DecOpForType(use_type->name);
    if (!op_name) {
      if (error) *error = "unsupported inc/dec type '" + use_type->name + "'";
      return false;
    }
    if (expr.children[0].kind == ExprKind::Identifier) {
      const std::string& name = expr.children[0].text;
      if (st.captured_locals.find(name) != st.captured_locals.end() ||
          st.current_upvalues.find(name) != st.current_upvalues.end()) {
        Expr one;
        one.kind = ExprKind::Literal;
        one.literal_kind = LiteralKind::Integer;
        one.text = "1";
        return EmitCapturedAssignment(
            st, name, *use_type, one, expr.op == "++" ? "+=" : "-=", true, error);
      }
      auto it = st.local_indices.find(name);
      if (it == st.local_indices.end()) {
        if (error) *error = "unknown local '" + expr.children[0].text + "'";
        return false;
      }
      (*st.out) << "  ldloc " << it->second << "\n";
      PushStack(st, 1);
      (*st.out) << "  " << op_name << "\n";
      (*st.out) << "  dup\n";
      PushStack(st, 1);
      (*st.out) << "  stloc " << it->second << "\n";
      PopStack(st, 1);
      return true;
    }
    if (expr.children[0].kind == ExprKind::Index) {
      const Expr& target = expr.children[0];
      if (target.children.size() != 2) {
        if (error) *error = "index expression expects target and index";
        return false;
      }
      TypeRef container_type;
      if (!InferExprType(target.children[0], st, &container_type, error)) return false;
      if (container_type.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays and lists";
        return false;
      }
      TypeRef element_type;
      if (!CloneElementType(container_type, &element_type)) {
        if (error) *error = "failed to resolve index element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type, st);
      if (!op_suffix) {
        if (error) *error = "unsupported index element type for SIR emission";
        return false;
      }
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      TypeRef index_type;
      index_type.name = "i32";
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      if (!EmitIndexGetOp(st, container_type, op_suffix)) return false;
      (*st.out) << "  " << op_name << "\n";
      if (!EmitDup(st)) return false;
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      (*st.out) << "  rot\n";
      return EmitIndexSetOp(st, container_type, op_suffix);
    }
    if (expr.children[0].kind == ExprKind::Member) {
      const Expr& target = expr.children[0];
      if (target.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = target.children[0];
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      if (target.op == "->") {
        if (base_type.pointer_depth == 0) {
          if (error) *error = "pointer member access requires a pointer type";
          return false;
        }
        base_type.pointer_depth -= 1;
      }
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << target.text << "\n";
      (*st.out) << "  " << op_name << "\n";
      if (!EmitDup(st)) return false;
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  swap\n";
      (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
      PopStack(st, 2);
      return true;
    }
    if (error) *error = "inc/dec target not supported in SIR emission";
    return false;
  }
  if (expr.op == "post++" || expr.op == "post--") {
    const char* op_name = expr.op == "post++" ? IncOpForType(use_type->name) : DecOpForType(use_type->name);
    if (!op_name) {
      if (error) *error = "unsupported inc/dec type '" + use_type->name + "'";
      return false;
    }
    if (expr.children[0].kind == ExprKind::Identifier) {
      const std::string& name = expr.children[0].text;
      if (st.captured_locals.find(name) != st.captured_locals.end() ||
          st.current_upvalues.find(name) != st.current_upvalues.end()) {
        if (!EmitCaptureCellLoad(st, name, *use_type, error)) return false;
        Expr one;
        one.kind = ExprKind::Literal;
        one.literal_kind = LiteralKind::Integer;
        one.text = "1";
        return EmitCapturedAssignment(
            st, name, *use_type, one, expr.op == "post++" ? "+=" : "-=", false, error);
      }
      auto it = st.local_indices.find(name);
      if (it == st.local_indices.end()) {
        if (error) *error = "unknown local '" + expr.children[0].text + "'";
        return false;
      }
      (*st.out) << "  ldloc " << it->second << "\n";
      PushStack(st, 1);
      (*st.out) << "  dup\n";
      PushStack(st, 1);
      (*st.out) << "  " << op_name << "\n";
      (*st.out) << "  stloc " << it->second << "\n";
      PopStack(st, 1);
      return true;
    }
    if (expr.children[0].kind == ExprKind::Index) {
      const Expr& target = expr.children[0];
      if (target.children.size() != 2) {
        if (error) *error = "index expression expects target and index";
        return false;
      }
      TypeRef container_type;
      if (!InferExprType(target.children[0], st, &container_type, error)) return false;
      if (container_type.dims.empty()) {
        if (error) *error = "indexing is only valid on arrays and lists";
        return false;
      }
      TypeRef element_type;
      if (!CloneElementType(container_type, &element_type)) {
        if (error) *error = "failed to resolve index element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type, st);
      if (!op_suffix) {
        if (error) *error = "unsupported index element type for SIR emission";
        return false;
      }
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      TypeRef index_type;
      index_type.name = "i32";
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      if (!EmitIndexGetOp(st, container_type, op_suffix)) return false;
      if (!EmitDup(st)) return false;
      (*st.out) << "  " << op_name << "\n";
      if (!EmitExpr(st, target.children[0], &container_type, error)) return false;
      if (!EmitExpr(st, target.children[1], &index_type, error)) return false;
      (*st.out) << "  rot\n";
      return EmitIndexSetOp(st, container_type, op_suffix);
    }
    if (expr.children[0].kind == ExprKind::Member) {
      const Expr& target = expr.children[0];
      if (target.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = target.children[0];
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      if (target.op == "->") {
        if (base_type.pointer_depth == 0) {
          if (error) *error = "pointer member access requires a pointer type";
          return false;
        }
        base_type.pointer_depth -= 1;
      }
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << target.text << "\n";
      if (!EmitDup(st)) return false;
      (*st.out) << "  " << op_name << "\n";
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  swap\n";
      (*st.out) << "  stfld " << base_type.name << "." << target.text << "\n";
      PopStack(st, 2);
      return true;
    }
    if (error) *error = "inc/dec target not supported in SIR emission";
    return false;
  }
  if (!EmitExpr(st, expr.children[0], use_type, error)) return false;
  if (expr.op == "-" && TAST::IsNumericScalarTypeName(use_type->name)) {
    (*st.out) << "  neg " << use_type->name << "\n";
    return true;
  }
  if (expr.op == "!" && use_type->name == "bool") {
    (*st.out) << "  bool.not\n";
    return true;
  }
  if (error) *error = "unsupported unary operator '" + expr.op + "'";
  return false;
}

bool EmitBinary(EmitState& st,
                const Expr& expr,
                const TypeRef* expected,
                std::string* error) {
  if (expr.children.size() < 2) {
    if (error) *error = "binary missing operands";
    return false;
  }
  TypeRef left_type;
  TypeRef right_type;
  if (!InferBinaryOperandTypes(expr, st, &left_type, &right_type, error)) return false;
  const bool is_cmp =
      (expr.op == "==" || expr.op == "!=" || expr.op == "<" || expr.op == "<=" ||
       expr.op == ">" || expr.op == ">=");
  if (left_type.name != right_type.name && (!expected || is_cmp)) {
    const bool lhs_lit = IsIntegerLiteralExpr(expr.children[0]);
    const bool rhs_lit = IsIntegerLiteralExpr(expr.children[1]);
    const bool lhs_int = TAST::IsIntegerScalarTypeName(left_type.name);
    const bool rhs_int = TAST::IsIntegerScalarTypeName(right_type.name);
    if (lhs_lit && rhs_int) {
      if (!CloneTypeRef(right_type, &left_type)) return false;
    } else if (rhs_lit && lhs_int) {
      if (!CloneTypeRef(left_type, &right_type)) return false;
    } else if (IsFloatLiteralExpr(expr.children[0]) && TAST::IsFloatTypeName(right_type.name)) {
      if (!CloneTypeRef(right_type, &left_type)) return false;
    } else if (IsFloatLiteralExpr(expr.children[1]) && TAST::IsFloatTypeName(left_type.name)) {
      if (!CloneTypeRef(left_type, &right_type)) return false;
    } else {
      if (error) {
        *error = "operand type mismatch for '" + expr.op + "': " +
                 GEN::TypeRefIdentity(left_type) + " and " + GEN::TypeRefIdentity(right_type);
      }
      return false;
    }
  }

  if (expr.op == "=" || AssignOpToBinaryOp(expr.op)) {
    if (expected) {
      if (error) *error = "assignment expression not supported in typed context";
      return false;
    }
    return EmitAssignmentExpr(st, expr, error);
  }

  if (expr.op == "&&" || expr.op == "||") {
    TypeRef bool_type;
    bool_type.name = "bool";
    if (!EmitExpr(st, expr.children[0], &bool_type, error)) return false;
    std::string short_label = NewLabel(st, expr.op == "&&" ? "and_false_" : "or_true_");
    std::string end_label = NewLabel(st, "bool_end_");
    if (expr.op == "&&") {
      (*st.out) << "  jmp.false " << short_label << "\n";
      PopStack(st, 1);
      if (!EmitExpr(st, expr.children[1], &bool_type, error)) return false;
      (*st.out) << "  jmp.false " << short_label << "\n";
      PopStack(st, 1);
      (*st.out) << "  const bool 1\n";
      PushStack(st, 1);
      (*st.out) << "  jmp " << end_label << "\n";
      (*st.out) << short_label << ":\n";
      (*st.out) << "  const bool 0\n";
      PushStack(st, 1);
      (*st.out) << end_label << ":\n";
      return true;
    }
    (*st.out) << "  jmp.true " << short_label << "\n";
    PopStack(st, 1);
    if (!EmitExpr(st, expr.children[1], &bool_type, error)) return false;
    (*st.out) << "  jmp.true " << short_label << "\n";
    PopStack(st, 1);
    (*st.out) << "  const bool 0\n";
    PushStack(st, 1);
    (*st.out) << "  jmp " << end_label << "\n";
    (*st.out) << short_label << ":\n";
    (*st.out) << "  const bool 1\n";
    PushStack(st, 1);
    (*st.out) << end_label << ":\n";
    return true;
  }

  TypeRef type;
  if (!CloneTypeRef(left_type, &type)) {
    if (error) *error = "failed to clone type";
    return false;
  }
  if (expected && !is_cmp) {
    if (!CloneTypeRef(*expected, &type)) {
      if (error) *error = "failed to clone expected type";
      return false;
    }
  }

  if (!EmitExpr(st, expr.children[0], &type, error)) return false;
  if (!EmitExpr(st, expr.children[1], &type, error)) return false;
  PopStack(st, 1);
  if (expr.op == "==" || expr.op == "!=" || expr.op == "<" || expr.op == "<=" ||
      expr.op == ">" || expr.op == ">=") {
    if (type.name == "string" && (expr.op == "==" || expr.op == "!=")) {
      (*st.out) << (expr.op == "==" ? "  string.eq\n" : "  string.ne\n");
      return true;
    }
    const bool is_enum = st.enum_values.find(type.name) != st.enum_values.end();
    const char* op_type = is_enum ? "i32" : NormalizeNumericOpType(type.name);
    if (!op_type) {
      if (error) *error = "unsupported operand type for '" + expr.op + "'";
      return false;
    }
    if (type.name == "bool") {
      if (error) *error = "bool comparisons not supported in SIR emission";
      return false;
    }
    const char* cmp = nullptr;
    if (expr.op == "==") cmp = "cmp.eq ";
    else if (expr.op == "!=") cmp = "cmp.ne ";
    else if (expr.op == "<") cmp = "cmp.lt ";
    else if (expr.op == "<=") cmp = "cmp.le ";
    else if (expr.op == ">") cmp = "cmp.gt ";
    else if (expr.op == ">=") cmp = "cmp.ge ";
    (*st.out) << "  " << cmp << op_type << "\n";
    return true;
  }
  if (expr.op == "+" || expr.op == "-" || expr.op == "*" || expr.op == "/" || expr.op == "%") {
    const char* op_type = NormalizeNumericOpType(type.name);
    if (!op_type) {
      if (error) *error = "unsupported operand type for '" + expr.op + "'";
      return false;
    }
    if (expr.op == "+" ) {
    (*st.out) << "  add " << op_type << "\n";
    return true;
  }
    if (expr.op == "-") {
      (*st.out) << "  sub " << op_type << "\n";
      return true;
    }
    if (expr.op == "*") {
      (*st.out) << "  mul " << op_type << "\n";
      return true;
    }
    if (expr.op == "/") {
      (*st.out) << "  div " << op_type << "\n";
      return true;
    }
    if (expr.op == "%" && TAST::IsIntegerScalarTypeName(type.name)) {
      (*st.out) << "  mod " << op_type << "\n";
      return true;
    }
  }
  if (expr.op == "&" || expr.op == "|" || expr.op == "^" || expr.op == "<<" || expr.op == ">>") {
    const char* op_type = NormalizeBitwiseOpType(type.name);
    if (!op_type) {
      if (error) *error = "unsupported operand type for '" + expr.op + "'";
      return false;
    }
    if (expr.op == "&") {
      (*st.out) << "  and " << op_type << "\n";
    } else if (expr.op == "|") {
      (*st.out) << "  or " << op_type << "\n";
    } else if (expr.op == "^") {
      (*st.out) << "  xor " << op_type << "\n";
    } else if (expr.op == "<<") {
      (*st.out) << "  shl " << op_type << "\n";
    } else if (expr.op == ">>") {
      (*st.out) << "  shr " << op_type << "\n";
    }
    return true;
  }
  if (error) *error = "unsupported binary operator '" + expr.op + "'";
  return false;
}

struct LocalScopeSnapshot {
  std::unordered_map<std::string, TypeRef> local_types;
  std::unordered_map<std::string, std::string> local_dl_modules;
  std::unordered_map<std::string, uint16_t> local_indices;
};

LocalScopeSnapshot SaveLocalScope(const EmitState& st) {
  return {st.local_types, st.local_dl_modules, st.local_indices};
}

void RestoreLocalScope(EmitState& st, LocalScopeSnapshot snapshot) {
  st.local_types = std::move(snapshot.local_types);
  st.local_dl_modules = std::move(snapshot.local_dl_modules);
  st.local_indices = std::move(snapshot.local_indices);
}

bool EmitSwitchExpr(EmitState& st,
                    const Expr& expr,
                    const TypeRef* expected,
                    std::string* error) {
  if (expr.children.empty()) {
    if (error) *error = "invalid switch expression";
    return false;
  }
  TypeRef switch_type;
  if (expected) {
    if (!CloneTypeRef(*expected, &switch_type)) return false;
  } else {
    if (!InferExprType(expr, st, &switch_type, error)) return false;
  }
  TypeRef subject_type;
  if (!InferExprType(expr.children[0], st, &subject_type, error)) return false;
  bool uses_patterns = false;
  for (const auto& branch : expr.switch_branches) {
    uses_patterns = uses_patterns || IsSwitchPattern(branch);
  }
  if (uses_patterns) {
    TaggedTypeInfo tagged;
    if (!ResolveTaggedType(subject_type, st, &tagged)) {
      if (error) *error = "structural switch patterns require optional or Result subject";
      return false;
    }
    std::string subject_temp_name;
    uint16_t subject_temp_index = 0;
    if (!AllocateTempLocal(
            st, subject_type, &subject_temp_name, &subject_temp_index, error)) {
      return false;
    }
    if (!EmitExpr(st, expr.children[0], &subject_type, error)) return false;
    (*st.out) << "  stloc " << subject_temp_index << "\n";
    PopStack(st, 1);
    const uint32_t branch_stack_base = st.stack_cur;
    const std::string end_label = NewLabel(st, "switch_end_");
    for (size_t branch_index = 0; branch_index < expr.switch_branches.size();
         ++branch_index) {
      const auto& branch = expr.switch_branches[branch_index];
      const bool has_next_branch = branch_index + 1 < expr.switch_branches.size();
      const std::string next_label =
          has_next_branch ? NewLabel(st, "switch_next_") : std::string();
      if (has_next_branch && tagged.kind == TaggedArtifactKind::Optional) {
        (*st.out) << "  ldloc " << subject_temp_index << "\n";
        PushStack(st, 1);
        (*st.out) << "  isnull\n";
        if (branch.pattern_kind == SwitchPatternKind::Absent) {
          (*st.out) << "  jmp.false " << next_label << "\n";
        } else {
          (*st.out) << "  jmp.true " << next_label << "\n";
        }
        PopStack(st, 1);
      } else if (has_next_branch) {
        const int tag = branch.pattern_field == "error" ? 1 : 0;
        (*st.out) << "  ldloc " << subject_temp_index << "\n";
        PushStack(st, 1);
        (*st.out) << "  ldfld " << subject_type.name << ".tag\n";
        (*st.out) << "  const i32 " << tag << "\n";
        PushStack(st, 1);
        (*st.out) << "  cmp.eq i32\n";
        PopStack(st, 1);
        (*st.out) << "  jmp.false " << next_label << "\n";
        PopStack(st, 1);
      }

      auto branch_scope = SaveLocalScope(st);
      const TypeRef* binding_type = nullptr;
      std::string binding_field;
      if (branch.pattern_kind == SwitchPatternKind::Present) {
        binding_type = tagged.value_type;
        binding_field = "value";
      } else if (branch.pattern_kind == SwitchPatternKind::Tagged) {
        binding_field = branch.pattern_field;
        binding_type = binding_field == "error" ? tagged.error_type : tagged.value_type;
      }
      if (binding_type) {
        if (st.local_indices.find(branch.pattern_binding) != st.local_indices.end()) {
          st.local_indices.erase(branch.pattern_binding);
          st.local_types.erase(branch.pattern_binding);
        }
        const uint16_t binding_index = st.next_local++;
        st.local_indices[branch.pattern_binding] = binding_index;
        TypeRef binding_copy;
        if (!CloneTypeRef(*binding_type, &binding_copy)) return false;
        st.local_types[branch.pattern_binding] = std::move(binding_copy);
        (*st.out) << "  ldloc " << subject_temp_index << "\n";
        PushStack(st, 1);
        (*st.out) << "  ldfld " << subject_type.name << "." << binding_field << "\n";
        (*st.out) << "  stloc " << binding_index << "\n";
        PopStack(st, 1);
      }

      if (branch.is_block) {
        if (branch.block.empty() ||
            branch.block.back().kind != StmtKind::Return ||
            !branch.block.back().has_return_expr) {
          if (error) *error = "switch branch block must end with a return value";
          return false;
        }
        for (size_t stmt_index = 0; stmt_index + 1 < branch.block.size(); ++stmt_index) {
          if (!EmitStmt(st, branch.block[stmt_index], error)) return false;
        }
        if (!EmitExpr(st, branch.block.back().expr, &switch_type, error)) return false;
      } else {
        if (!branch.has_inline_value) {
          if (error) *error = "switch branch requires a value";
          return false;
        }
        if (!EmitExpr(st, branch.value, &switch_type, error)) return false;
      }
      (*st.out) << "  jmp " << end_label << "\n";
      RestoreLocalScope(st, std::move(branch_scope));
      st.stack_cur = branch_stack_base;
      if (has_next_branch) (*st.out) << next_label << ":\n";
    }
    (*st.out) << end_label << ":\n";
    st.stack_cur = branch_stack_base + 1;
    st.stack_max = std::max(st.stack_max, st.stack_cur);
    return true;
  }

  if (!EmitExpr(st, expr.children[0], nullptr, error)) return false;
  (*st.out) << "  pop\n";
  PopStack(st, 1);
  std::string end_label = NewLabel(st, "switch_end_");
  for (size_t i = 0; i < expr.switch_branches.size(); ++i) {
    const auto& branch = expr.switch_branches[i];
    std::string next_label = NewLabel(st, "switch_next_");
    if (!branch.is_default) {
      if (!EmitExpr(st, branch.condition, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << next_label << "\n";
      PopStack(st, 1);
    }
    if (branch.is_block) {
      if (branch.block.empty() ||
          branch.block.back().kind != StmtKind::Return ||
          !branch.block.back().has_return_expr) {
        if (error) *error = "switch branch block must end with a return value";
        return false;
      }
      auto branch_scope = SaveLocalScope(st);
      for (size_t stmt_index = 0; stmt_index + 1 < branch.block.size(); ++stmt_index) {
        if (!EmitStmt(st, branch.block[stmt_index], error)) return false;
      }
      if (!EmitExpr(st, branch.block.back().expr, &switch_type, error)) return false;
      RestoreLocalScope(st, std::move(branch_scope));
      (*st.out) << "  jmp " << end_label << "\n";
    } else {
      if (!branch.has_inline_value) {
        if (error) *error = "switch branch requires a value";
        return false;
      }
      if (!EmitExpr(st, branch.value, &switch_type, error)) return false;
      (*st.out) << "  jmp " << end_label << "\n";
    }
    if (!branch.is_default) {
      (*st.out) << next_label << ":\n";
    }
  }
  (*st.out) << end_label << ":\n";
  return true;
}

bool EmitExpr(EmitState& st,
              const Expr& expr,
              const TypeRef* expected,
              std::string* error) {
  switch (expr.kind) {
    case ExprKind::Identifier: {
      auto it = st.local_indices.find(expr.text);
      if (it != st.local_indices.end()) {
        auto type_it = st.local_types.find(expr.text);
        if (st.captured_locals.find(expr.text) != st.captured_locals.end() &&
            type_it != st.local_types.end()) {
          return EmitCaptureCellLoad(st, expr.text, type_it->second, error);
        }
        (*st.out) << "  ldloc " << it->second << "\n";
        return PushStack(st, 1);
      }
      auto upvalue_it = st.current_upvalues.find(expr.text);
      if (upvalue_it != st.current_upvalues.end()) {
        return EmitCaptureCellLoad(st, expr.text, upvalue_it->second.type, error);
      }
      auto git = st.global_indices.find(expr.text);
      if (git != st.global_indices.end()) {
        (*st.out) << "  ldglob " << git->second << "\n";
        return PushStack(st, 1);
      }
      if (error) *error = "unknown local '" + expr.text + "'";
      return false;
    }
    case ExprKind::Literal: {
      TypeRef literal_type;
      if (!InferLiteralType(expr, &literal_type)) {
        if (error) *error = "unknown literal type";
        return false;
      }
      const TypeRef* use_type = expected ? expected : &literal_type;
      if (!IsSupportedType(*use_type) || use_type->name == "void") {
        if (error) *error = "literal type not supported in SIR emission";
        return false;
      }
      return EmitConstForType(st, *use_type, expr, error);
    }
    case ExprKind::Call: {
      if (expr.children.empty()) {
        if (error) *error = "call missing callee";
        return false;
      }
      const Expr& callee = expr.children[0];
      auto emit_indirect_call = [&](const TypeRef& proc_type,
                                    const std::string& target_name) -> bool {
        if (!proc_type.is_proc || !proc_type.proc_return) {
          if (error) *error = "call target is not a procedure: " + target_name;
          return false;
        }
        if (expr.args.size() != proc_type.proc_params.size()) {
          if (error) *error = "call argument count mismatch for '" + target_name + "'";
          return false;
        }
        for (size_t i = 0; i < proc_type.proc_params.size(); ++i) {
          if (!EmitExpr(st, expr.args[i], &proc_type.proc_params[i], error)) return false;
        }
        if (!EmitExpr(st, callee, &proc_type, error)) return false;
        const std::string sig_name = GetProcSigName(st, proc_type, error);
        if (sig_name.empty()) return false;
        (*st.out) << "  call.indirect " << sig_name << " "
                  << proc_type.proc_params.size() << "\n";
        PopStack(st, static_cast<uint32_t>(proc_type.proc_params.size() + 1));
        if (proc_type.proc_return->name != "void") PushStack(st, 1);
        return true;
      };
      if (callee.kind == ExprKind::Identifier) {
        std::string using_module;
        if (ResolveUsingReservedMember(st, callee.text, &using_module)) {
          if (IsCanonicalLibraryModule(using_module, StandardModule::IO) && IsIoPrintName(callee.text)) {
            if (expr.args.empty()) {
              if (error) *error = "call argument count mismatch for '" + callee.text + "'";
              return false;
            }
            if (expr.args.size() == 1) {
              TypeRef arg_type;
              if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
              if (!EmitPrintAnyValue(st, expr.args[0], arg_type, error)) return false;
            } else {
              const Expr& fmt_expr = expr.args[0];
              if (!(fmt_expr.kind == ExprKind::Literal && fmt_expr.literal_kind == LiteralKind::String)) {
                if (error) *error = "Standard.IO.print format call expects string literal as first argument";
                return false;
              }
              size_t placeholder_count = 0;
              std::vector<std::string> segments;
              if (!CountFormatPlaceholders(fmt_expr.text, &placeholder_count, &segments, error)) return false;
              if (placeholder_count != expr.args.size() - 1) {
                if (error) *error = "Standard.IO.print format placeholder count mismatch";
                return false;
              }
              for (size_t i = 0; i < placeholder_count; ++i) {
                if (!segments[i].empty()) {
                  TypeRef seg_type = MakeTypeRef("string");
                  Expr seg_expr;
                  seg_expr.kind = ExprKind::Literal;
                  seg_expr.literal_kind = LiteralKind::String;
                  seg_expr.text = segments[i];
                  if (!EmitPrintAnyValue(st, seg_expr, seg_type, error)) return false;
                }
                TypeRef arg_type;
                if (!InferExprType(expr.args[i + 1], st, &arg_type, error)) return false;
                if (!EmitPrintAnyValue(st, expr.args[i + 1], arg_type, error)) return false;
              }
              if (!segments.empty() && !segments.back().empty()) {
                TypeRef seg_type = MakeTypeRef("string");
                Expr seg_expr;
                seg_expr.kind = ExprKind::Literal;
                seg_expr.literal_kind = LiteralKind::String;
                seg_expr.text = segments.back();
                if (!EmitPrintAnyValue(st, seg_expr, seg_type, error)) return false;
              }
            }
            if (callee.text == "println") {
              if (!EmitPrintNewline(st, error)) return false;
            }
            return true;
          }
          const auto math_member = ParseMember(StandardModule::Math, callee.text);
          if (IsCanonicalLibraryModule(using_module, StandardModule::Math) &&
              math_member &&
              (std::get<StandardMathMember>(*math_member) == StandardMathMember::Abs ||
               std::get<StandardMathMember>(*math_member) == StandardMathMember::Sqrt)) {
            if (expr.args.size() != 1) {
              if (error) *error = "call argument count mismatch for '" + callee.text + "'";
              return false;
            }
            TypeRef arg_type;
            if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
            if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
            uint32_t id = 0;
            if (callee.text == "abs" && arg_type.name == "i32") id = Simple::VM::kIntrinsicAbsI32;
            else if (callee.text == "abs" && arg_type.name == "i64") id = Simple::VM::kIntrinsicAbsI64;
            else if (callee.text == "sqrt" && arg_type.name == "f32") id = Simple::VM::kIntrinsicSqrtF32;
            else if (callee.text == "sqrt" && arg_type.name == "f64") id = Simple::VM::kIntrinsicSqrtF64;
            else {
              if (error) *error = "Math." + callee.text + " argument type is unsupported";
              return false;
            }
            (*st.out) << "  intrinsic " << id << "\n";
            PopStack(st, 1);
            PushStack(st, 1);
            return true;
          }
          if ((IsCanonicalLibraryModule(using_module, SystemModule::Time) ||
               IsCanonicalLibraryModule(using_module, StandardModule::Time)) &&
              (callee.text == "mono_ns" || callee.text == "wall_ns")) {
            if (!expr.args.empty()) {
              if (error) *error = "call argument count mismatch for '" + callee.text + "'";
              return false;
            }
            (*st.out) << "  intrinsic "
                      << (callee.text == "mono_ns" ? Simple::VM::kIntrinsicMonoNs : Simple::VM::kIntrinsicWallNs)
                      << "\n";
            PushStack(st, 1);
            return true;
          }
          if (IsNativeReservedModule(using_module)) {
            auto ext_mod_it = st.extern_ids_by_module.find(using_module);
            const std::string qualified_name = using_module + "." + callee.text;
            if (ext_mod_it == st.extern_ids_by_module.end()) {
              if (error) *error = "missing extern module for '" + qualified_name + "'";
              return false;
            }
            auto id_it = ext_mod_it->second.find(callee.text);
            if (id_it == ext_mod_it->second.end()) {
              if (error) *error = "missing extern id for '" + qualified_name + "'";
              return false;
            }
            auto params_it = st.extern_params_by_module[using_module].find(callee.text);
            auto ret_it = st.extern_returns_by_module[using_module].find(callee.text);
            if (params_it == st.extern_params_by_module[using_module].end() ||
                ret_it == st.extern_returns_by_module[using_module].end()) {
              if (error) *error = "missing signature for extern '" + qualified_name + "'";
              return false;
            }
            const auto& params = params_it->second;
            if (expr.args.size() != params.size()) {
              if (error) *error = "call argument count mismatch for '" + qualified_name + "'";
              return false;
            }
            for (size_t i = 0; i < params.size(); ++i) {
              if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
            }
            (*st.out) << "  call " << id_it->second << " " << params.size() << "\n";
            if (st.stack_cur >= params.size()) st.stack_cur -= static_cast<uint32_t>(params.size());
            else st.stack_cur = 0;
            if (ret_it->second.name != "void") PushStack(st, 1);
            return true;
          }
          if (using_module == "Math" && (callee.text == "min" || callee.text == "max")) {
            if (expr.args.size() != 2) {
              if (error) *error = "call argument count mismatch for '" + callee.text + "'";
              return false;
            }
            TypeRef arg_type;
            if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
            if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
            if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
            uint32_t id = 0;
            if (arg_type.name == "i32") id = (callee.text == "min") ? Simple::VM::kIntrinsicMinI32 : Simple::VM::kIntrinsicMaxI32;
            else if (arg_type.name == "i64") id = (callee.text == "min") ? Simple::VM::kIntrinsicMinI64 : Simple::VM::kIntrinsicMaxI64;
            else if (arg_type.name == "f32") id = (callee.text == "min") ? Simple::VM::kIntrinsicMinF32 : Simple::VM::kIntrinsicMaxF32;
            else if (arg_type.name == "f64") id = (callee.text == "min") ? Simple::VM::kIntrinsicMinF64 : Simple::VM::kIntrinsicMaxF64;
            else {
              if (error) *error = "Math." + callee.text + " argument type is unsupported";
              return false;
            }
            (*st.out) << "  intrinsic " << id << "\n";
            PopStack(st, 2);
            PushStack(st, 1);
            return true;
          }
        }
        if (ResolveUsingModuleExternMember(st, callee.text, &using_module)) {
          auto ext_mod_it = st.extern_ids_by_module.find(using_module);
          const std::string qualified_name = using_module + "." + callee.text;
          if (ext_mod_it == st.extern_ids_by_module.end()) {
            if (error) *error = "missing extern module for '" + qualified_name + "'";
            return false;
          }
          auto id_it = ext_mod_it->second.find(callee.text);
          auto params_it = st.extern_params_by_module[using_module].find(callee.text);
          auto ret_it = st.extern_returns_by_module[using_module].find(callee.text);
          if (id_it == ext_mod_it->second.end() ||
              params_it == st.extern_params_by_module[using_module].end() ||
              ret_it == st.extern_returns_by_module[using_module].end()) {
            if (error) *error = "missing signature for extern '" + qualified_name + "'";
            return false;
          }
          const auto& params = params_it->second;
          if (expr.args.size() != params.size()) {
            if (error) *error = "call argument count mismatch for '" + qualified_name + "'";
            return false;
          }
          std::string handle_global;
          if (FindDlHandleGlobalForModule(st, using_module, &handle_global)) {
            return EmitDynamicDlCallByHandleGlobal(st, handle_global, using_module, callee.text, expr.args, error);
          }
          for (size_t i = 0; i < params.size(); ++i) {
            if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
          }
          (*st.out) << "  call " << id_it->second << " " << params.size() << "\n";
          if (st.stack_cur >= params.size()) st.stack_cur -= static_cast<uint32_t>(params.size());
          else st.stack_cur = 0;
          if (ret_it->second.name != "void") PushStack(st, 1);
          return true;
        }
      }
      if (callee.kind == ExprKind::Member && callee.op == "." && !callee.children.empty()) {
        const Expr& base = callee.children[0];
        TypeRef member_callee_type;
        if (InferExprType(callee, st, &member_callee_type, nullptr) &&
            member_callee_type.is_proc) {
          return emit_indirect_call(member_callee_type, callee.text);
        }
        if (IsIoPrintCallExpr(callee, st)) {
          if (expr.args.empty()) {
            if (error) *error = "call argument count mismatch for 'IO." + callee.text + "'";
            return false;
          }
          if (expr.args.size() == 1) {
            TypeRef arg_type;
            if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
            if (!EmitPrintAnyValue(st, expr.args[0], arg_type, error)) return false;
          } else {
            const Expr& fmt_expr = expr.args[0];
            if (!(fmt_expr.kind == ExprKind::Literal &&
                  fmt_expr.literal_kind == LiteralKind::String)) {
              if (error) *error = "Standard.IO.print format call expects string literal as first argument";
              return false;
            }
            size_t placeholder_count = 0;
            std::vector<std::string> segments;
            if (!CountFormatPlaceholders(fmt_expr.text, &placeholder_count, &segments, error)) {
              return false;
            }
            if (placeholder_count != expr.args.size() - 1) {
              if (error) {
                *error = "Standard.IO.print format placeholder count mismatch: expected " +
                         std::to_string(placeholder_count) + ", got " +
                         std::to_string(expr.args.size() - 1);
              }
              return false;
            }
            for (size_t i = 0; i < placeholder_count; ++i) {
              if (!segments[i].empty()) {
                TypeRef seg_type = MakeTypeRef("string");
                Expr seg_expr;
                seg_expr.kind = ExprKind::Literal;
                seg_expr.literal_kind = LiteralKind::String;
                seg_expr.text = segments[i];
                if (!EmitPrintAnyValue(st, seg_expr, seg_type, error)) return false;
              }
              TypeRef arg_type;
              if (!InferExprType(expr.args[i + 1], st, &arg_type, error)) return false;
              if (!EmitPrintAnyValue(st, expr.args[i + 1], arg_type, error)) return false;
            }
            if (!segments.empty() && !segments.back().empty()) {
              TypeRef seg_type = MakeTypeRef("string");
              Expr seg_expr;
              seg_expr.kind = ExprKind::Literal;
              seg_expr.literal_kind = LiteralKind::String;
              seg_expr.text = segments.back();
              if (!EmitPrintAnyValue(st, seg_expr, seg_type, error)) return false;
            }
          }
          if (callee.text == "println") {
            if (!EmitPrintNewline(st, error)) return false;
          }
          return true;
        }
        if (base.kind == ExprKind::Identifier) {
          std::string dl_module;
          ResolveDlModuleForIdentifier(base.text, st, &dl_module);
          if (!dl_module.empty()) {
            auto params_mod_it = st.extern_params_by_module.find(dl_module);
            auto returns_mod_it = st.extern_returns_by_module.find(dl_module);
            if (params_mod_it == st.extern_params_by_module.end() ||
                returns_mod_it == st.extern_returns_by_module.end()) {
              if (error) *error = "unknown dynamic DL manifest module: " + dl_module;
              return false;
            }
            auto params_it = params_mod_it->second.find(callee.text);
            auto ret_it = returns_mod_it->second.find(callee.text);
            if (params_it == params_mod_it->second.end() || ret_it == returns_mod_it->second.end()) {
              if (error) *error = "unknown dynamic symbol: " + base.text + "." + callee.text;
              return false;
            }
            const auto& params = params_it->second;
            if (expr.args.size() != params.size()) {
              if (error) *error = "call argument count mismatch for dynamic symbol '" +
                                  base.text + "." + callee.text + "'";
              return false;
            }
            const EmitState::AbiTypeInfo* abi_ret = nullptr;
            if (NeedsAbiFlattenType(ret_it->second, st)) {
              abi_ret = FindAbiTypeForArtifact(st, ret_it->second.name);
              if (!abi_ret) {
                if (error) *error = "missing ABI type for dynamic return '" + callee.text + "'";
                return false;
              }
            }
            auto call_mod_it = st.dl_call_import_ids_by_module.find(dl_module);
            if (call_mod_it == st.dl_call_import_ids_by_module.end()) {
              if (error) *error = "missing dynamic DL call import module: " + dl_module;
              return false;
            }
            auto call_id_it = call_mod_it->second.find(callee.text);
            if (call_id_it == call_mod_it->second.end()) {
              if (error) *error = "missing dynamic DL call import: " + dl_module + "." + callee.text;
              return false;
            }
            std::string sym_import_id;
            if (!GetCoreDlSymImportId(st, &sym_import_id)) {
              if (error) *error = "missing DL.sym import for dynamic symbol calls";
              return false;
            }
            TypeRef ptr_type = MakeTypeRef("i64");
            if (!EmitExpr(st, base, &ptr_type, error)) return false;
            std::string symbol_name;
            if (!AddStringConst(st, callee.text, &symbol_name)) return false;
            (*st.out) << "  const string " << symbol_name << "\n";
            PushStack(st, 1);
            (*st.out) << "  call " << sym_import_id << " 2\n";
            PopStack(st, 2);
            PushStack(st, 1);
            uint32_t abi_arg_count = 1;
            for (size_t i = 0; i < params.size(); ++i) {
              const EmitState::AbiTypeInfo* abi_param = nullptr;
              if (NeedsAbiFlattenType(params[i], st)) {
                abi_param = FindAbiTypeForArtifact(st, params[i].name);
                if (!abi_param) {
                  if (error) *error = "missing ABI type for dynamic param '" + callee.text + "'";
                  return false;
                }
              }
              if (abi_param) {
                if (!EmitAbiPackArtifactArg(st, expr.args[i], params[i], *abi_param, error)) return false;
              } else {
                if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
              }
              ++abi_arg_count;
            }
            if (abi_arg_count > 255) {
              if (error) *error = "dynamic DL call has too many ABI parameters";
              return false;
            }
            (*st.out) << "  call " << call_id_it->second << " " << abi_arg_count << "\n";
            PopStack(st, abi_arg_count);
            if (ret_it->second.name != "void") PushStack(st, 1);
            if (abi_ret) {
              if (!EmitAbiInflateReturn(st, *abi_ret, ret_it->second.name, error)) return false;
            }
            return true;
          }
        }
        TypeRef list_type;
        if (InferExprType(base, st, &list_type, nullptr) &&
            !list_type.dims.empty() && list_type.dims.front().is_list) {
          const std::string member_name = callee.text;
          TypeRef element_type;
          if (!CloneElementType(list_type, &element_type)) return false;
          auto emit_list_value = [&](const Expr& expr_value,
                                     const TypeRef& type) -> bool {
            return EmitExpr(st, expr_value, &type, error);
          };
          if (member_name == "len") {
            if (expr.args.size() != 0) {
              if (error) *error = "call argument count mismatch for 'list.len'";
              return false;
            }
            if (!emit_list_value(base, list_type)) return false;
            (*st.out) << "  list.len\n";
            PopStack(st, 1);
            PushStack(st, 1);
            return true;
          }
          if (member_name == "push") {
            if (expr.args.size() != 1) {
              if (error) *error = "call argument count mismatch for 'list.push'";
              return false;
            }
            const char* op_suffix = VmOpSuffixForType(element_type, st);
            if (!op_suffix) {
              if (error) *error = "unsupported list element type for list.push";
              return false;
            }
            if (!emit_list_value(base, list_type)) return false;
            if (!emit_list_value(expr.args[0], element_type)) return false;
            (*st.out) << "  list.push " << op_suffix << "\n";
            PopStack(st, 2);
            return true;
          }
          if (member_name == "pop") {
            if (expr.args.size() != 0) {
              if (error) *error = "call argument count mismatch for 'list.pop'";
              return false;
            }
            const char* op_suffix = VmOpSuffixForType(element_type, st);
            if (!op_suffix) {
              if (error) *error = "unsupported list element type for list.pop";
              return false;
            }
            if (!emit_list_value(base, list_type)) return false;
            (*st.out) << "  list.pop " << op_suffix << "\n";
            PopStack(st, 1);
            PushStack(st, 1);
            return true;
          }
          if (member_name == "insert") {
            if (expr.args.size() != 2) {
              if (error) *error = "call argument count mismatch for 'list.insert'";
              return false;
            }
            const char* op_suffix = VmOpSuffixForType(element_type, st);
            if (!op_suffix) {
              if (error) *error = "unsupported list element type for list.insert";
              return false;
            }
            TypeRef index_type = MakeTypeRef("i32");
            if (!emit_list_value(base, list_type)) return false;
            if (!emit_list_value(expr.args[0], index_type)) return false;
            if (!emit_list_value(expr.args[1], element_type)) return false;
            (*st.out) << "  list.insert " << op_suffix << "\n";
            PopStack(st, 3);
            return true;
          }
          if (member_name == "remove") {
            if (expr.args.size() != 1) {
              if (error) *error = "call argument count mismatch for 'list.remove'";
              return false;
            }
            const char* op_suffix = VmOpSuffixForType(element_type, st);
            if (!op_suffix) {
              if (error) *error = "unsupported list element type for list.remove";
              return false;
            }
            TypeRef index_type = MakeTypeRef("i32");
            if (!emit_list_value(base, list_type)) return false;
            if (!emit_list_value(expr.args[0], index_type)) return false;
            (*st.out) << "  list.remove " << op_suffix << "\n";
            PopStack(st, 2);
            PushStack(st, 1);
            return true;
          }
          if (member_name == "clear") {
            if (expr.args.size() != 0) {
              if (error) *error = "call argument count mismatch for 'list.clear'";
              return false;
            }
            if (!emit_list_value(base, list_type)) return false;
            (*st.out) << "  list.clear\n";
            PopStack(st, 1);
            return true;
          }
        }
        std::string module_name;
        if (GetModuleNameFromExpr(base, &module_name)) {
          LibraryModuleId reserved_module_id{};
          if (!ResolveReservedModuleId(st, module_name, &reserved_module_id)) {
            // Not a reserved module; fall through to normal call handling.
          } else {
            const std::string reserved_module = std::string(ToCanonicalName(reserved_module_id));
            if (IsLibraryModule(reserved_module_id, StandardModule::Math)) {
              if (callee.text == "abs") {
                if (expr.args.size() != 1) {
                  if (error) *error = "call argument count mismatch for 'Math.abs'";
                  return false;
                }
                TypeRef arg_type;
                if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
                uint32_t id = 0;
                if (arg_type.name == "i32") {
                  id = Simple::VM::kIntrinsicAbsI32;
                } else if (arg_type.name == "i64") {
                  id = Simple::VM::kIntrinsicAbsI64;
                } else {
                  if (error) *error = "Math.abs expects i32 or i64";
                  return false;
                }
                (*st.out) << "  intrinsic " << id << "\n";
                PopStack(st, 1);
                PushStack(st, 1);
                return true;
              }
              if (callee.text == "sqrt") {
                if (expr.args.size() != 1) {
                  if (error) *error = "call argument count mismatch for 'Math.sqrt'";
                  return false;
                }
                TypeRef arg_type;
                if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
                uint32_t id = 0;
                if (arg_type.name == "f32") {
                  id = Simple::VM::kIntrinsicSqrtF32;
                } else if (arg_type.name == "f64") {
                  id = Simple::VM::kIntrinsicSqrtF64;
                } else {
                  if (error) *error = "Math.sqrt expects f32 or f64";
                  return false;
                }
                (*st.out) << "  intrinsic " << id << "\n";
                PopStack(st, 1);
                PushStack(st, 1);
                return true;
              }
            }
            const bool reserved_is_ffi = IsLibraryModule(reserved_module_id, SystemModule::FFI);
            const std::string member_name =
                reserved_is_ffi ? NormalizeCoreDlMember(callee.text) : callee.text;
            if (reserved_is_ffi) {
              if (member_name == "open") {
                if (expr.args.size() != 1 && expr.args.size() != 2) {
                  if (error) *error = "call argument count mismatch for 'System.FFI.open'";
                  return false;
                }
                auto ext_mod_it = st.extern_ids_by_module.find(reserved_module);
                if (ext_mod_it == st.extern_ids_by_module.end()) {
                  if (error) *error = "missing extern module for 'System.FFI.open'";
                  return false;
                }
                auto id_it = ext_mod_it->second.find(member_name);
                if (id_it == ext_mod_it->second.end()) {
                  if (error) *error = "missing extern id for 'System.FFI.open'";
                  return false;
                }
                auto params_it = st.extern_params_by_module[reserved_module].find(member_name);
                auto ret_it = st.extern_returns_by_module[reserved_module].find(member_name);
                if (params_it == st.extern_params_by_module[reserved_module].end() ||
                    ret_it == st.extern_returns_by_module[reserved_module].end()) {
                  if (error) *error = "missing signature for extern 'System.FFI.open'";
                  return false;
                }
                const auto& params = params_it->second;
                if (params.size() != 1) {
                  if (error) *error = "invalid extern signature for 'System.FFI.open'";
                  return false;
                }
                if (!EmitExpr(st, expr.args[0], &params[0], error)) return false;
                (*st.out) << "  call " << id_it->second << " 1\n";
                if (st.stack_cur >= 1) st.stack_cur -= 1;
                else st.stack_cur = 0;
                if (ret_it->second.name != "void") PushStack(st, 1);
                return true;
              }
              if (member_name == "call_i32") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'DL.call_i32'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("i32");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallI32 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_i64") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'DL.call_i64'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("i64");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallI64 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_f32") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'DL.call_f32'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("f32");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallF32 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_f64") {
                if (expr.args.size() != 3) {
                  if (error) *error = "call argument count mismatch for 'DL.call_f64'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                TypeRef arg_type = MakeTypeRef("f64");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
                if (!EmitExpr(st, expr.args[2], &arg_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallF64 << "\n";
                PopStack(st, 3);
                PushStack(st, 1);
                return true;
              }
              if (member_name == "call_str0") {
                if (expr.args.size() != 1) {
                  if (error) *error = "call argument count mismatch for 'DL.call_str0'";
                  return false;
                }
                TypeRef ptr_type = MakeTypeRef("i64");
                if (!EmitExpr(st, expr.args[0], &ptr_type, error)) return false;
                (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicDlCallStr0 << "\n";
                PopStack(st, 1);
                PushStack(st, 1);
                return true;
              }
            }
            if (member_name == "min" || member_name == "max") {
              if (expr.args.size() != 2) {
                if (error) *error = "call argument count mismatch for 'Math." + callee.text + "'";
                return false;
              }
              TypeRef arg_type;
              if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
              if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
              if (!EmitExpr(st, expr.args[1], &arg_type, error)) return false;
              uint32_t id = 0;
              if (arg_type.name == "i32") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinI32 : Simple::VM::kIntrinsicMaxI32;
              } else if (arg_type.name == "i64") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinI64 : Simple::VM::kIntrinsicMaxI64;
              } else if (arg_type.name == "f32") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinF32 : Simple::VM::kIntrinsicMaxF32;
              } else if (arg_type.name == "f64") {
                id = (member_name == "min") ? Simple::VM::kIntrinsicMinF64 : Simple::VM::kIntrinsicMaxF64;
              } else {
                if (error) *error = "Math." + callee.text + " expects numeric type";
                return false;
              }
              (*st.out) << "  intrinsic " << id << "\n";
              PopStack(st, 2);
              PushStack(st, 1);
              return true;
            }
          }
          if (IsLibraryModule(reserved_module_id, SystemModule::Time) ||
              IsLibraryModule(reserved_module_id, StandardModule::Time)) {
            if (callee.text == "mono_ns") {
              if (!expr.args.empty()) {
                if (error) *error = "Time.mono_ns expects no arguments";
                return false;
              }
              (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicMonoNs << "\n";
              PushStack(st, 1);
              return true;
            }
            if (callee.text == "wall_ns") {
              if (!expr.args.empty()) {
                if (error) *error = "Time.wall_ns expects no arguments";
                return false;
              }
              (*st.out) << "  intrinsic " << Simple::VM::kIntrinsicWallNs << "\n";
              PushStack(st, 1);
              return true;
            }
          }
        }
        if (GetModuleNameFromExpr(base, &module_name)) {
          LibraryModuleId resolved_module_id{};
          const bool module_is_reserved = ResolveReservedModuleId(st, module_name, &resolved_module_id);
          const bool module_is_system_ffi =
              (ParseCanonicalLibraryModule(module_name) &&
               IsCanonicalLibraryModule(module_name, SystemModule::FFI)) ||
              (module_is_reserved && IsLibraryModule(resolved_module_id, SystemModule::FFI));
          const std::string member_name =
              module_is_system_ffi ? NormalizeCoreDlMember(callee.text) : callee.text;
          const std::string key = module_name + "." + member_name;
          auto module_it = st.module_func_names.find(key);
          if (module_it != st.module_func_names.end()) {
            const std::string& hoisted = module_it->second;
            auto params_it = st.func_params.find(hoisted);
            if (params_it == st.func_params.end()) {
              if (error) *error = "missing signature for '" + key + "'";
              return false;
            }
            const auto& params = params_it->second;
            if (expr.args.size() != params.size()) {
              if (error) *error = "call argument count mismatch for '" + key + "'";
              return false;
            }
            for (size_t i = 0; i < params.size(); ++i) {
              if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
            }
            auto id_it = st.func_ids.find(hoisted);
            if (id_it == st.func_ids.end()) {
              if (error) *error = "unknown function '" + key + "'";
              return false;
            }
            EmitDirectCallOpcode(st, hoisted, id_it->second, params.size());
            if (st.stack_cur >= params.size()) {
              st.stack_cur -= static_cast<uint32_t>(params.size());
            } else {
              st.stack_cur = 0;
            }
            auto ret_it = st.func_returns.find(hoisted);
            if (st.async_funcs.find(hoisted) != st.async_funcs.end() ||
                (ret_it != st.func_returns.end() && ret_it->second.name != "void")) {
              PushStack(st, 1);
            }
            return true;
          }
          std::string ext_module_name = module_name;
          LibraryModuleId resolved_module_id_for_ext{};
          const bool has_resolved_module_for_ext =
              ResolveReservedModuleId(st, module_name, &resolved_module_id_for_ext);
          std::string resolved_module_name_for_ext;
          if (has_resolved_module_for_ext) resolved_module_name_for_ext = std::string(ToCanonicalName(resolved_module_id_for_ext));
          bool ext_is_system_ffi =
              (ParseCanonicalLibraryModule(ext_module_name) &&
               IsCanonicalLibraryModule(ext_module_name, SystemModule::FFI)) ||
              (has_resolved_module_for_ext && IsLibraryModule(resolved_module_id_for_ext, SystemModule::FFI));
          auto ext_mod_it = st.extern_ids_by_module.find(ext_module_name);
          if (ext_mod_it == st.extern_ids_by_module.end()) {
            if (has_resolved_module_for_ext) {
              ext_module_name = resolved_module_name_for_ext;
              ext_mod_it = st.extern_ids_by_module.find(ext_module_name);
            }
          }
          if (ext_mod_it != st.extern_ids_by_module.end()) {
            const std::string extern_member_name =
                ext_is_system_ffi ? NormalizeCoreDlMember(callee.text) : callee.text;
            const std::string ext_key = ext_module_name + "." + extern_member_name;
            auto id_it = ext_mod_it->second.find(extern_member_name);
            if (id_it != ext_mod_it->second.end()) {
              auto params_it = st.extern_params_by_module[ext_module_name].find(extern_member_name);
              auto ret_it = st.extern_returns_by_module[ext_module_name].find(extern_member_name);
              if (params_it == st.extern_params_by_module[ext_module_name].end() ||
                  ret_it == st.extern_returns_by_module[ext_module_name].end()) {
                if (error) *error = "missing signature for extern '" + ext_key + "'";
                return false;
              }
              const auto& params = params_it->second;
              if (expr.args.size() != params.size()) {
                if (error) *error = "call argument count mismatch for '" + ext_key + "'";
                return false;
              }
              std::string handle_global;
              if (FindDlHandleGlobalForModule(st, ext_module_name, &handle_global)) {
                return EmitDynamicDlCallByHandleGlobal(st, handle_global, ext_module_name, extern_member_name, expr.args, error);
              }
              const EmitState::AbiTypeInfo* abi_ret = nullptr;
              if (NeedsAbiFlattenType(ret_it->second, st)) {
                abi_ret = FindAbiTypeForArtifact(st, ret_it->second.name);
                if (!abi_ret) {
                  if (error) *error = "missing ABI type for extern return '" + ext_key + "'";
                  return false;
                }
              }
              for (size_t i = 0; i < params.size(); ++i) {
                const EmitState::AbiTypeInfo* abi_param = nullptr;
                if (NeedsAbiFlattenType(params[i], st)) {
                  abi_param = FindAbiTypeForArtifact(st, params[i].name);
                  if (!abi_param) {
                    if (error) *error = "missing ABI type for extern param '" + ext_key + "'";
                    return false;
                  }
                }
                if (abi_param) {
                  if (!EmitAbiPackArtifactArg(st, expr.args[i], params[i], *abi_param, error)) return false;
                } else {
                  if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
                }
              }
              (*st.out) << "  call " << id_it->second << " " << params.size() << "\n";
              if (st.stack_cur >= params.size()) {
                st.stack_cur -= static_cast<uint32_t>(params.size());
              } else {
                st.stack_cur = 0;
              }
              if (ret_it->second.name != "void") {
                PushStack(st, 1);
              }
              if (abi_ret) {
                if (!EmitAbiInflateReturn(st, *abi_ret, ret_it->second.name, error)) return false;
              }
              return true;
            }
          }
        }
        TypeRef base_type;
        if (!InferExprType(base, st, &base_type, nullptr)) {
          if (error) *error = "call target not supported in SIR emission";
          return false;
        }
        if (base_type.name == "Promise" && base_type.type_args.size() == 1 &&
            base_type.dims.empty() &&
            (callee.text == "cancel" || callee.text == "isDone" ||
             callee.text == "isCancelled")) {
          if (!expr.args.empty()) {
            if (error) *error = "Promise control method expects no arguments";
            return false;
          }
          if (!EmitExpr(st, base, &base_type, error)) return false;
          if (callee.text == "cancel") {
            (*st.out) << "  future.cancel\n";
            return true;
          }
          (*st.out) << "  future.poll\n";
          (*st.out) << "  const i32 " << (callee.text == "isCancelled" ? 2 : 0) << "\n";
          PushStack(st, 1);
          (*st.out) << (callee.text == "isCancelled" ? "  cmp.eq i32\n"
                                                      : "  cmp.ne i32\n");
          PopStack(st, 1);
          return true;
        }
        const std::string key = base_type.name + "." + callee.text;
        auto method_it = st.artifact_method_names.find(key);
        if (method_it != st.artifact_method_names.end()) {
          const std::string& hoisted = method_it->second;
          auto params_it = st.func_params.find(hoisted);
          if (params_it == st.func_params.end()) {
            if (error) *error = "missing signature for '" + key + "'";
            return false;
          }
          const auto& params = params_it->second;
          if (expr.args.size() + 1 != params.size()) {
            if (error) *error = "call argument count mismatch for '" + key + "'";
            return false;
          }
          if (!EmitExpr(st, base, &base_type, error)) return false;
          for (size_t i = 0; i < expr.args.size(); ++i) {
            if (!EmitExpr(st, expr.args[i], &params[i + 1], error)) return false;
          }
          auto id_it = st.func_ids.find(hoisted);
          if (id_it == st.func_ids.end()) {
            if (error) *error = "unknown function '" + key + "'";
            return false;
          }
          EmitDirectCallOpcode(st, hoisted, id_it->second, params.size());
          if (st.stack_cur >= params.size()) {
            st.stack_cur -= static_cast<uint32_t>(params.size());
          } else {
            st.stack_cur = 0;
          }
          auto ret_it = st.func_returns.find(hoisted);
          if (st.async_funcs.find(hoisted) != st.async_funcs.end() ||
              (ret_it != st.func_returns.end() && ret_it->second.name != "void")) {
            PushStack(st, 1);
          }
          return true;
        }
      }
      if (callee.kind == ExprKind::FnLiteral) {
        if (!expected) {
          if (error) *error = "direct fn literal call requires a typed result context";
          return false;
        }
        if (callee.fn_params.size() != expr.args.size()) {
          if (error) *error = "call argument count mismatch for fn literal";
          return false;
        }
        TypeRef call_type;
        call_type.is_proc = true;
        call_type.proc_return = std::make_unique<TypeRef>();
        if (!CloneTypeRef(*expected, call_type.proc_return.get())) return false;
        for (size_t i = 0; i < expr.args.size(); ++i) {
          TypeRef param_type;
          if (!callee.fn_params[i].type.name.empty() || callee.fn_params[i].type.is_proc) {
            if (!CloneTypeRef(callee.fn_params[i].type, &param_type)) return false;
          } else if (!InferExprType(expr.args[i], st, &param_type, error)) {
            if (error && error->empty()) {
              *error = "cannot infer direct fn literal parameter type";
            }
            return false;
          }
          call_type.proc_params.push_back(std::move(param_type));
        }
        return emit_indirect_call(call_type, "fn literal");
      }
      const std::string& name = callee.text;
      if (name == "len") {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for 'len'";
          return false;
        }
        TypeRef arg_type;
        if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
        if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
        if (arg_type.name == "string" && arg_type.dims.empty()) {
          (*st.out) << "  string.len\n";
        } else if (!arg_type.dims.empty()) {
          if (arg_type.dims.front().is_list) {
            (*st.out) << "  list.len\n";
          } else {
            (*st.out) << "  array.len\n";
          }
        } else {
          if (error) *error = "len expects array, list, or string argument";
          return false;
        }
        PopStack(st, 1);
        PushStack(st, 1);
        return true;
      }
      std::string cast_target;
      if (GetAtCastTargetName(name, &cast_target)) {
        if (expr.args.size() != 1) {
          if (error) *error = "call argument count mismatch for '" + cast_target + "'";
          return false;
        }
        TypeRef arg_type;
        if (!InferExprType(expr.args[0], st, &arg_type, error)) return false;
        if (!EmitExpr(st, expr.args[0], &arg_type, error)) return false;
        if (cast_target == "string") {
          if (!arg_type.dims.empty() ||
              (!TAST::IsNumericScalarTypeName(arg_type.name) && arg_type.name != "bool")) {
            if (error) *error = "string cast expects numeric or bool argument";
            return false;
          }
          uint32_t id = 0;
          if (arg_type.name == "i8" || arg_type.name == "i16" || arg_type.name == "i32") {
            id = Simple::VM::kIntrinsicStrI32;
          } else if (arg_type.name == "i64") {
            id = Simple::VM::kIntrinsicStrI64;
          } else if (arg_type.name == "u8" || arg_type.name == "u16" || arg_type.name == "u32") {
            id = Simple::VM::kIntrinsicStrU32;
          } else if (arg_type.name == "u64") {
            id = Simple::VM::kIntrinsicStrU64;
          } else if (arg_type.name == "f32") {
            id = Simple::VM::kIntrinsicStrF32;
          } else if (arg_type.name == "f64") {
            id = Simple::VM::kIntrinsicStrF64;
          } else if (arg_type.name == "bool") {
            id = Simple::VM::kIntrinsicStrBool;
          } else {
            if (error) *error = "string cast expects numeric or bool argument";
            return false;
          }
          (*st.out) << "  intrinsic " << id << "\n";
          PopStack(st, 1);
          PushStack(st, 1);
          return true;
        }
        CastVmKind src = GetCastVmKind(arg_type.name);
        CastVmKind dst = GetCastVmKind(cast_target);
        if (src == CastVmKind::Invalid || dst == CastVmKind::Invalid) {
          if (error) *error = "unsupported cast in SIR emission: " + arg_type.name + " -> " + cast_target;
          return false;
        }
        if (src != dst) {
          if (src == CastVmKind::I32 && dst == CastVmKind::I64) {
            (*st.out) << "  conv i32 i64\n";
          } else if (src == CastVmKind::I64 && dst == CastVmKind::I32) {
            (*st.out) << "  conv i64 i32\n";
          } else if (src == CastVmKind::I32 && dst == CastVmKind::F32) {
            (*st.out) << "  conv i32 f32\n";
          } else if (src == CastVmKind::I32 && dst == CastVmKind::F64) {
            (*st.out) << "  conv i32 f64\n";
          } else if (src == CastVmKind::F32 && dst == CastVmKind::I32) {
            (*st.out) << "  conv f32 i32\n";
          } else if (src == CastVmKind::F64 && dst == CastVmKind::I32) {
            (*st.out) << "  conv f64 i32\n";
          } else if (src == CastVmKind::F32 && dst == CastVmKind::F64) {
            (*st.out) << "  conv f32 f64\n";
          } else if (src == CastVmKind::F64 && dst == CastVmKind::F32) {
            (*st.out) << "  conv f64 f32\n";
          } else {
            if (error) *error = "unsupported cast in SIR emission: " + arg_type.name + " -> " + cast_target;
            return false;
          }
        } else if (arg_type.name != cast_target) {
          // Normalize same-lane casts (for example i8 -> i32) to produce verifier-visible dst kind.
          if (dst == CastVmKind::I32 && cast_target == "i32") {
            if (arg_type.name == "bool") {
              if (error) *error = "unsupported cast in SIR emission: " + arg_type.name + " -> " + cast_target;
              return false;
            }
            (*st.out) << "  const i32 0\n";
            PushStack(st, 1);
            (*st.out) << "  add i32\n";
            PopStack(st, 2);
            PushStack(st, 1);
          } else if (dst == CastVmKind::I64 && cast_target == "i64" && arg_type.name == "u64") {
            (*st.out) << "  const i64 -1\n";
            PushStack(st, 1);
            (*st.out) << "  and i64\n";
            PopStack(st, 2);
            PushStack(st, 1);
          }
        }
        return true;
      }
      if (callee.kind == ExprKind::Identifier) {
        auto local_it = st.local_types.find(name);
        if (local_it != st.local_types.end()) {
          return emit_indirect_call(local_it->second, name);
        }
        auto upvalue_it = st.current_upvalues.find(name);
        if (upvalue_it != st.current_upvalues.end()) {
          return emit_indirect_call(upvalue_it->second.type, name);
        }
        auto global_it = st.global_types.find(name);
        if (global_it != st.global_types.end() && global_it->second.is_proc) {
          return emit_indirect_call(global_it->second, name);
        }
        auto ext_it = st.extern_ids.find(name);
        if (ext_it != st.extern_ids.end()) {
          auto params_it = st.extern_params.find(name);
          auto ret_it = st.extern_returns.find(name);
          if (params_it == st.extern_params.end() || ret_it == st.extern_returns.end()) {
            if (error) *error = "missing signature for extern '" + name + "'";
            return false;
          }
          const auto& params = params_it->second;
          if (expr.args.size() != params.size()) {
            if (error) *error = "call argument count mismatch for '" + name + "'";
            return false;
          }
          const EmitState::AbiTypeInfo* abi_ret = nullptr;
          if (NeedsAbiFlattenType(ret_it->second, st)) {
            abi_ret = FindAbiTypeForArtifact(st, ret_it->second.name);
            if (!abi_ret) {
              if (error) *error = "missing ABI type for extern return '" + name + "'";
              return false;
            }
          }
          uint32_t abi_arg_count = 0;
          for (size_t i = 0; i < params.size(); ++i) {
            const EmitState::AbiTypeInfo* abi_param = nullptr;
            if (NeedsAbiFlattenType(params[i], st)) {
              abi_param = FindAbiTypeForArtifact(st, params[i].name);
              if (!abi_param) {
                if (error) *error = "missing ABI type for extern param '" + name + "'";
                return false;
              }
            }
            if (abi_param) {
              if (!EmitAbiPackArtifactArg(st, expr.args[i], params[i], *abi_param, error)) return false;
            } else {
              if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
            }
            ++abi_arg_count;
          }
          (*st.out) << "  call " << ext_it->second << " " << abi_arg_count << "\n";
          if (st.stack_cur >= abi_arg_count) {
            st.stack_cur -= abi_arg_count;
          } else {
            st.stack_cur = 0;
          }
          if (ret_it->second.name != "void") {
            PushStack(st, 1);
          }
          if (abi_ret) {
            if (!EmitAbiInflateReturn(st, *abi_ret, ret_it->second.name, error)) return false;
          }
          return true;
        }
        auto id_it = st.func_ids.find(name);
        if (id_it == st.func_ids.end()) {
          if (error) *error = "unknown function '" + name + "'";
          return false;
        }
        auto params_it = st.func_params.find(name);
        if (params_it == st.func_params.end()) {
          if (error) *error = "missing signature for '" + name + "'";
          return false;
        }
        const auto& params = params_it->second;
        if (expr.args.size() != params.size()) {
          if (error) *error = "call argument count mismatch for '" + name + "'";
          return false;
        }
        for (size_t i = 0; i < params.size(); ++i) {
          if (!EmitExpr(st, expr.args[i], &params[i], error)) return false;
        }
        EmitDirectCallOpcode(st, name, id_it->second, params.size());
        if (st.stack_cur >= params.size()) {
          st.stack_cur -= static_cast<uint32_t>(params.size());
        } else {
          st.stack_cur = 0;
        }
        auto ret_it = st.func_returns.find(name);
        if (st.async_funcs.find(name) != st.async_funcs.end() ||
            (ret_it != st.func_returns.end() && ret_it->second.name != "void")) {
          PushStack(st, 1);
        }
        return true;
      }

      TypeRef callee_type;
      if (!InferExprType(callee, st, &callee_type, error)) return false;
      if (!callee_type.is_proc) {
        if (error) *error = "call target not supported in SIR emission";
        return false;
      }
      return emit_indirect_call(callee_type, "callee");
    }
    case ExprKind::FormatString: {
      size_t placeholder_count = 0;
      std::vector<std::string> segments;
      if (!CountFormatPlaceholders(expr.text, &placeholder_count, &segments, error)) return false;
      if (placeholder_count != expr.args.size()) {
        if (error) {
          *error = "format placeholder count mismatch: expected " +
                   std::to_string(placeholder_count) + ", got " +
                   std::to_string(expr.args.size());
        }
        return false;
      }
      TypeRef string_type;
      string_type.name = "string";
      auto emit_string_literal = [&](const std::string& text) -> bool {
        Expr seg_expr;
        seg_expr.kind = ExprKind::Literal;
        seg_expr.literal_kind = LiteralKind::String;
        seg_expr.text = text;
        return EmitExpr(st, seg_expr, &string_type, error);
      };
      auto emit_string_value = [&](const Expr& value) -> bool {
        TypeRef arg_type;
        if (!InferExprType(value, st, &arg_type, error)) return false;
        if (arg_type.dims.empty() && arg_type.name == "string") {
          return EmitExpr(st, value, &string_type, error);
        }
        if (!arg_type.dims.empty() ||
            (!TAST::IsNumericScalarTypeName(arg_type.name) && arg_type.name != "bool")) {
          if (error) *error = "format supports numeric, bool, or string";
          return false;
        }
        if (!EmitExpr(st, value, &arg_type, error)) return false;
        uint32_t id = 0;
        if (arg_type.name == "i8" || arg_type.name == "i16" || arg_type.name == "i32") {
          id = Simple::VM::kIntrinsicStrI32;
        } else if (arg_type.name == "i64") {
          id = Simple::VM::kIntrinsicStrI64;
        } else if (arg_type.name == "u8" || arg_type.name == "u16" || arg_type.name == "u32") {
          id = Simple::VM::kIntrinsicStrU32;
        } else if (arg_type.name == "u64") {
          id = Simple::VM::kIntrinsicStrU64;
        } else if (arg_type.name == "f32") {
          id = Simple::VM::kIntrinsicStrF32;
        } else if (arg_type.name == "f64") {
          id = Simple::VM::kIntrinsicStrF64;
        } else if (arg_type.name == "bool") {
          id = Simple::VM::kIntrinsicStrBool;
        } else {
          if (error) *error = "format supports numeric, bool, or string";
          return false;
        }
        (*st.out) << "  intrinsic " << id << "\n";
        PopStack(st, 1);
        PushStack(st, 1);
        return true;
      };
      const std::string first_seg = segments.empty() ? std::string() : segments[0];
      if (!emit_string_literal(first_seg)) return false;
      for (size_t i = 0; i < placeholder_count; ++i) {
        if (!emit_string_value(expr.args[i])) return false;
        (*st.out) << "  string.concat\n";
        PopStack(st, 2);
        PushStack(st, 1);
        if (i + 1 < segments.size() && !segments[i + 1].empty()) {
          if (!emit_string_literal(segments[i + 1])) return false;
          (*st.out) << "  string.concat\n";
          PopStack(st, 2);
          PushStack(st, 1);
        }
      }
      return true;
    }
    case ExprKind::Unary:
      return EmitUnary(st, expr, expected, error);
    case ExprKind::Binary:
      return EmitBinary(st, expr, expected, error);
    case ExprKind::Switch:
      return EmitSwitchExpr(st, expr, expected, error);
    case ExprKind::ArrayLiteral:
    case ExprKind::ListLiteral: {
      if (!expected) {
        if (error) *error = "array/list literal requires expected type";
        return false;
      }
      if (expected->dims.empty()) {
        if (error) *error = "array/list literal requires array or list type";
        return false;
      }
      bool is_list = expected->dims.front().is_list;
      if (expr.kind == ExprKind::ListLiteral && !is_list) {
        if (error) *error = "list literal requires list type";
        return false;
      }
      TypeRef element_type;
      if (!CloneElementType(*expected, &element_type)) {
        if (error) *error = "failed to resolve array/list element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type, st);
      const char* type_name = VmTypeNameForElement(element_type, st);
      if (!op_suffix || !type_name) {
        if (error) *error = "unsupported array/list element type for SIR emission";
        return false;
      }
      uint32_t length = static_cast<uint32_t>(expr.children.size());
      if (is_list) {
        (*st.out) << "  newlist " << type_name << " " << length << "\n";
      } else {
        (*st.out) << "  newarray " << type_name << " " << length << "\n";
      }
      PushStack(st, 1);
      for (uint32_t i = 0; i < length; ++i) {
        (*st.out) << "  dup\n";
        PushStack(st, 1);
        if (!EmitExpr(st, expr.children[i], &element_type, error)) return false;
        if (is_list) {
          (*st.out) << "  list.push " << op_suffix << "\n";
          PopStack(st, 2);
        } else {
          (*st.out) << "  const i32 " << i << "\n";
          PushStack(st, 1);
          (*st.out) << "  swap\n";
          (*st.out) << "  array.set " << op_suffix << "\n";
          PopStack(st, 3);
        }
      }
      return true;
    }
    case ExprKind::Index: {
      if (expr.children.size() != 2) {
        if (error) *error = "index expression expects target and index";
        return false;
      }
      TypeRef container_type;
      if (!InferExprType(expr.children[0], st, &container_type, error)) return false;
      const bool is_string = container_type.name == "string" && container_type.dims.empty();
      if (container_type.dims.empty() && !is_string) {
        if (error) *error = "indexing is only valid on arrays, lists, and strings";
        return false;
      }
      TypeRef element_type;
      if (is_string) {
        element_type.name = "char";
      } else if (!CloneElementType(container_type, &element_type)) {
        if (error) *error = "failed to resolve index element type";
        return false;
      }
      const char* op_suffix = VmOpSuffixForType(element_type, st);
      if (!op_suffix) {
        if (error) *error = "unsupported index element type for SIR emission";
        return false;
      }
      if (!EmitExpr(st, expr.children[0], &container_type, error)) return false;
      TypeRef index_type;
      index_type.name = "i32";
      if (!EmitExpr(st, expr.children[1], &index_type, error)) return false;
      if (is_string) {
        (*st.out) << "  string.get.char\n";
      } else if (container_type.dims.front().is_list) {
        (*st.out) << "  list.get " << op_suffix << "\n";
      } else {
        (*st.out) << "  array.get " << op_suffix << "\n";
      }
      PopStack(st, 2);
      PushStack(st, 1);
      return true;
    }
    case ExprKind::ArtifactLiteral: {
      if (expected &&
          !expected->dims.empty() &&
          !expected->dims.front().is_list &&
          expr.field_names.empty() &&
          expr.field_values.empty()) {
        bool is_list = false;
        TypeRef element_type;
        if (!CloneElementType(*expected, &element_type)) {
          if (error) *error = "failed to resolve array/list element type";
          return false;
        }
        const char* op_suffix = VmOpSuffixForType(element_type, st);
        const char* type_name = VmTypeNameForElement(element_type, st);
        if (!op_suffix || !type_name) {
          if (error) *error = "unsupported array/list element type for SIR emission";
          return false;
        }
        uint32_t length = static_cast<uint32_t>(expr.children.size());
        if (is_list) {
          (*st.out) << "  newlist " << type_name << " " << length << "\n";
        } else {
          (*st.out) << "  newarray " << type_name << " " << length << "\n";
        }
        PushStack(st, 1);
        for (uint32_t i = 0; i < length; ++i) {
          (*st.out) << "  dup\n";
          PushStack(st, 1);
          if (!EmitExpr(st, expr.children[i], &element_type, error)) return false;
          if (is_list) {
            (*st.out) << "  list.push " << op_suffix << "\n";
            PopStack(st, 2);
          } else {
            (*st.out) << "  const i32 " << i << "\n";
            PushStack(st, 1);
            (*st.out) << "  swap\n";
            (*st.out) << "  array.set " << op_suffix << "\n";
            PopStack(st, 3);
          }
        }
        return true;
      }
      if (!expected) {
        if (error) *error = "artifact literal requires expected type";
        return false;
      }
      TaggedTypeInfo tagged;
      if (ResolveTaggedType(*expected, st, &tagged)) {
        auto tagged_layout_it = st.artifact_layouts.find(expected->name);
        if (tagged_layout_it == st.artifact_layouts.end()) {
          if (error) *error = "tagged literal expects materialized tagged type";
          return false;
        }
        if (tagged.kind == TaggedArtifactKind::Optional) {
          if (!expr.field_names.empty() || !expr.field_values.empty() ||
              expr.children.size() > 1) {
            if (error) *error = "optional literal must be '{}' or '{ value }'";
            return false;
          }
          if (expr.children.empty()) {
            (*st.out) << "  const null\n";
            return PushStack(st, 1);
          }
          (*st.out) << "  newobj " << expected->name << "\n";
          PushStack(st, 1);
          (*st.out) << "  dup\n";
          PushStack(st, 1);
          if (!EmitExpr(st, expr.children[0], tagged.value_type, error)) return false;
          (*st.out) << "  stfld " << expected->name << ".value\n";
          PopStack(st, 2);
          return true;
        }
        if (expr.children.size() != 0 || expr.field_names.size() != 1 ||
            expr.field_values.size() != 1) {
          if (error) {
            *error = "Result literal requires exactly one '.value' or '.error' payload";
          }
          return false;
        }
        const bool is_error = expr.field_names[0] == "error";
        if (!is_error && expr.field_names[0] != "value") {
          if (error) *error = "Result literal field must be '.value' or '.error'";
          return false;
        }
        (*st.out) << "  newobj " << expected->name << "\n";
        PushStack(st, 1);
        for (const auto& field : tagged_layout_it->second.fields) {
          (*st.out) << "  dup\n";
          PushStack(st, 1);
          if (field.name == "tag") {
            (*st.out) << "  const i32 " << (is_error ? 1 : 0) << "\n";
            PushStack(st, 1);
          } else if ((field.name == "value" && !is_error) ||
                     (field.name == "error" && is_error)) {
            if (!EmitExpr(st, expr.field_values[0], &field.type, error)) return false;
          } else if (!EmitInactivePayload(st, field.type, error)) {
            return false;
          }
          (*st.out) << "  stfld " << expected->name << "." << field.name << "\n";
          PopStack(st, 2);
        }
        return true;
      }
      auto layout_it = st.artifact_layouts.find(expected->name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "artifact literal expects artifact type";
        return false;
      }
      const ArtifactDecl* artifact = nullptr;
      auto art_it = st.artifacts.find(expected->name);
      if (art_it != st.artifacts.end()) {
        artifact = art_it->second;
      }
      const auto& layout = layout_it->second;
      std::vector<const Expr*> field_exprs(layout.fields.size(), nullptr);
      if (!expr.children.empty()) {
        if (expr.children.size() > layout.fields.size()) {
          if (error) *error = "artifact literal has too many positional values";
          return false;
        }
        for (size_t i = 0; i < expr.children.size(); ++i) {
          field_exprs[i] = &expr.children[i];
        }
      }
      for (size_t i = 0; i < expr.field_names.size(); ++i) {
        const std::string& field = expr.field_names[i];
        auto field_it = layout.field_index.find(field);
        if (field_it == layout.field_index.end()) {
          if (error) *error = "unknown artifact field '" + field + "'";
          return false;
        }
        size_t index = field_it->second;
        field_exprs[index] = &expr.field_values[i];
      }
      (*st.out) << "  newobj " << expected->name << "\n";
      PushStack(st, 1);
      for (size_t i = 0; i < layout.fields.size(); ++i) {
        const auto& field = layout.fields[i];
        (*st.out) << "  dup\n";
        PushStack(st, 1);
        if (field_exprs[i]) {
          if (!EmitExpr(st, *field_exprs[i], &field.type, error)) return false;
        } else {
          if (artifact && i < artifact->fields.size() && artifact->fields[i].has_init_expr) {
            if (!EmitExpr(st, artifact->fields[i].init_expr, &field.type, error)) return false;
          } else {
            if (!EmitDefaultInit(st, field.type, error)) return false;
          }
        }
        (*st.out) << "  stfld " << expected->name << "." << field.name << "\n";
        PopStack(st, 2);
      }
      return true;
    }
    case ExprKind::FnLiteral: {
      if (!expected || !expected->is_proc) {
        if (error) *error = "fn literal requires a proc-typed context";
        return false;
      }
      if (expr.fn_params.size() != expected->proc_params.size()) {
        if (error) *error = "fn literal parameter count mismatch";
        return false;
      }
      std::vector<std::pair<std::string, TypeRef>> captures;
      const auto free_names = IRE::FindFnLiteralFreeNames(expr);
      std::vector<std::string> ordered_names(free_names.begin(), free_names.end());
      std::sort(ordered_names.begin(), ordered_names.end());
      for (const auto& name : ordered_names) {
        auto local_type_it = st.local_types.find(name);
        if (local_type_it != st.local_types.end()) {
          if (st.captured_locals.find(name) == st.captured_locals.end()) {
            if (error) *error = "captured local was not cell-lowered: " + name;
            return false;
          }
          TypeRef type;
          if (!CloneTypeRef(local_type_it->second, &type)) return false;
          captures.emplace_back(name, std::move(type));
          continue;
        }
        auto upvalue_it = st.current_upvalues.find(name);
        if (upvalue_it != st.current_upvalues.end()) {
          TypeRef type;
          if (!CloneTypeRef(upvalue_it->second.type, &type)) return false;
          captures.emplace_back(name, std::move(type));
        }
      }
      if (captures.size() > std::numeric_limits<uint8_t>::max()) {
        if (error) *error = "fn literal captures too many bindings";
        return false;
      }

      FuncDecl lambda;
      lambda.name = "__lambda" + std::to_string(st.lambda_counter++);
      lambda.return_mutability = expected->proc_return_mutability;
      if (expected->proc_return) {
        if (!CloneTypeRef(*expected->proc_return, &lambda.return_type)) return false;
      } else {
        lambda.return_type.name = "void";
      }
      lambda.params.clear();
      lambda.params.reserve(expr.fn_params.size());
      for (size_t i = 0; i < expr.fn_params.size(); ++i) {
        const auto& param = expr.fn_params[i];
        ParamDecl cloned_param;
        cloned_param.name = param.name;
        cloned_param.mutability = param.mutability;
        if (i < expected->proc_params.size()) {
          if (!CloneTypeRef(expected->proc_params[i], &cloned_param.type)) return false;
        } else {
          if (!CloneTypeRef(param.type, &cloned_param.type)) return false;
        }
        lambda.params.push_back(std::move(cloned_param));
      }

      lambda.body = expr.fn_body;

      uint32_t func_id = st.base_func_count + static_cast<uint32_t>(st.lambda_funcs.size());
      st.func_ids[lambda.name] = func_id;
      TypeRef ret;
      if (!CloneTypeRef(lambda.return_type, &ret)) return false;
      st.func_returns.emplace(lambda.name, std::move(ret));
      std::vector<TypeRef> params;
      params.reserve(lambda.params.size());
      for (const auto& param : lambda.params) {
        TypeRef cloned;
        if (!CloneTypeRef(param.type, &cloned)) return false;
        params.push_back(std::move(cloned));
      }
      st.func_params.emplace(lambda.name, std::move(params));
      st.lambda_captures[lambda.name] = captures;
      for (const auto& capture : captures) {
        if (!EmitCaptureCellRef(st, capture.first, error)) return false;
      }
      st.lambda_funcs.push_back(std::move(lambda));

      (*st.out) << "  newclosure " << st.lambda_funcs.back().name << " "
                << captures.size() << "\n";
      PopStack(st, static_cast<uint32_t>(captures.size()));
      return PushStack(st, 1);
    }
    case ExprKind::Member: {
      if (expr.children.empty()) {
        if (error) *error = "member access missing base";
        return false;
      }
      const Expr& base = expr.children[0];
      std::string module_name;
      LibraryModuleId module_id{};
      if (GetModuleNameFromExpr(base, &module_name) && ResolveReservedModuleId(st, module_name, &module_id)) {
        if (IsLibraryModule(module_id, StandardModule::Math) &&
            ParseMember(StandardModule::Math, expr.text) == StandardMember(StandardMathMember::PI)) {
          (*st.out) << "  const f64 3.141592653589793\n";
          return PushStack(st, 1);
        }
        if (IsLibraryModule(module_id, SystemModule::FFI) &&
            ParseMember(SystemModule::FFI, expr.text) == SystemMember(SystemFFIMember::Supported)) {
          (*st.out) << "  const i32 " << (HostHasDl() ? 1 : 0) << "\n";
          return PushStack(st, 1);
        }
      }
      if (base.kind == ExprKind::Identifier) {
        LibraryModuleId resolved{};
        if (ResolveReservedModuleId(st, base.text, &resolved) &&
            IsLibraryModule(resolved, SystemModule::FFI) &&
            ParseMember(SystemModule::FFI, expr.text) == SystemMember(SystemFFIMember::Supported)) {
          (*st.out) << "  const i32 " << (HostHasDl() ? 1 : 0) << "\n";
          return PushStack(st, 1);
        }
        if (ResolveReservedModuleId(st, base.text, &resolved) &&
            IsLibraryModule(resolved, SystemModule::OS) &&
            (expr.text == "is_linux" || expr.text == "is_macos" ||
             expr.text == "is_windows" || expr.text == "has_dl")) {
          bool value = false;
          if (expr.text == "is_linux") value = HostIsLinux();
          else if (expr.text == "is_macos") value = HostIsMacOs();
          else if (expr.text == "is_windows") value = HostIsWindows();
          else if (expr.text == "has_dl") value = HostHasDl();
          (*st.out) << "  const i32 " << (value ? 1 : 0) << "\n";
          return PushStack(st, 1);
        }
        auto enum_it = st.enum_values.find(base.text);
        if (enum_it != st.enum_values.end()) {
          auto member_it = enum_it->second.find(expr.text);
          if (member_it == enum_it->second.end()) {
            if (error) *error = "unknown enum member '" + expr.text + "'";
            return false;
          }
          (*st.out) << "  const i32 " << member_it->second << "\n";
          return PushStack(st, 1);
        }
        const std::string qualified = base.text + "." + expr.text;
        auto g_it = st.global_indices.find(qualified);
        if (g_it != st.global_indices.end()) {
          (*st.out) << "  ldglob " << g_it->second << "\n";
          return PushStack(st, 1);
        }
        if (st.module_func_names.find(qualified) != st.module_func_names.end()) {
          if (error) *error = "module function requires call: " + qualified;
          return false;
        }
        if (st.artifact_method_names.find(qualified) != st.artifact_method_names.end()) {
          if (error) *error = "artifact method requires call: " + qualified;
          return false;
        }
      }
      TypeRef base_type;
      if (!InferExprType(base, st, &base_type, error)) return false;
      if (expr.op == "->") {
        if (base_type.pointer_depth == 0) {
          if (error) *error = "pointer member access requires a pointer type";
          return false;
        }
        base_type.pointer_depth -= 1;
      }
      auto layout_it = st.artifact_layouts.find(base_type.name);
      if (layout_it == st.artifact_layouts.end()) {
        if (error) *error = "member access base is not an artifact";
        return false;
      }
      if (!EmitExpr(st, base, &base_type, error)) return false;
      (*st.out) << "  ldfld " << base_type.name << "." << expr.text << "\n";
      PopStack(st, 1);
      PushStack(st, 1);
      return true;
    }
    default:
      if (error) *error = "expression not supported for SIR emission";
      return false;
  }
}

bool EmitInactivePayload(EmitState& st, const TypeRef& type, std::string* error) {
  const char* suffix = VmOpSuffixForType(type, st);
  if (!suffix) {
    if (error) *error = "unsupported inactive payload type";
    return false;
  }
  if (std::string(suffix) == "ref") {
    (*st.out) << "  const null\n";
    return PushStack(st, 1);
  }
  return EmitDefaultInit(st, type, error);
}

bool NeedsRuntimeDefaultInitialization(const EmitState& st, const TypeRef& type) {
  if (!type.dims.empty()) return true;
  const auto artifact_it = st.artifacts.find(type.name);
  return artifact_it != st.artifacts.end() &&
         artifact_it->second->tagged_kind == TaggedArtifactKind::Result;
}

bool EmitDefaultInit(EmitState& st, const TypeRef& type, std::string* error) {
  if (!IsSupportedType(type) || type.name == "void") {
    if (error) *error = "unsupported default init type '" + type.name + "'";
    return false;
  }
  if (type.is_proc) {
    (*st.out) << "  const null\n";
    return PushStack(st, 1);
  }
  const auto artifact_it = st.artifacts.find(type.name);
  if (artifact_it != st.artifacts.end()) {
    if (artifact_it->second->tagged_kind != TaggedArtifactKind::Result) {
      (*st.out) << "  const null\n";
      return PushStack(st, 1);
    }
    const auto layout_it = st.artifact_layouts.find(type.name);
    if (layout_it == st.artifact_layouts.end()) {
      if (error) *error = "missing Result layout for default initialization";
      return false;
    }
    (*st.out) << "  newobj " << type.name << "\n";
    PushStack(st, 1);
    for (const auto& field : layout_it->second.fields) {
      (*st.out) << "  dup\n";
      PushStack(st, 1);
      const bool inactive_error = field.name == "error";
      if (inactive_error ? !EmitInactivePayload(st, field.type, error)
                         : !EmitDefaultInit(st, field.type, error)) {
        return false;
      }
      (*st.out) << "  stfld " << type.name << "." << field.name << "\n";
      PopStack(st, 2);
    }
    return true;
  }
  if (st.enum_values.find(type.name) != st.enum_values.end()) {
    (*st.out) << "  const i32 0\n";
    return PushStack(st, 1);
  }
  if (!type.dims.empty()) {
    TypeRef element_type;
    if (!CloneElementType(type, &element_type)) {
      if (error) *error = "failed to resolve default array/list element type";
      return false;
    }
    const char* type_name = VmTypeNameForElement(element_type, st);
    if (!type_name) {
      if (error) *error = "unsupported default array/list element type";
      return false;
    }
    const TypeDim& dim = type.dims.front();
    if (dim.is_list) {
      (*st.out) << "  newlist " << type_name << " 0\n";
    } else {
      uint64_t size = dim.has_size ? dim.size : 0;
      if (size > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        if (error) *error = "fixed array default size too large";
        return false;
      }
      (*st.out) << "  newarray " << type_name << " " << static_cast<uint32_t>(size) << "\n";
    }
    return PushStack(st, 1);
  }
  if (type.name == "Promise") {
    (*st.out) << "  const null\n";
    return PushStack(st, 1);
  }
  if (type.name == "string") {
    Expr expr;
    expr.kind = ExprKind::Literal;
    expr.literal_kind = LiteralKind::String;
    expr.text.clear();
    return EmitConstForType(st, type, expr, error);
  }
  Expr expr;
  expr.kind = ExprKind::Literal;
  expr.literal_kind = LiteralKind::Integer;
  expr.text = "0";
  return EmitConstForType(st, type, expr, error);
}

bool EmitBlock(EmitState& st, const std::vector<Stmt>& body, std::string* error) {
  for (const auto& stmt : body) {
    if (!EmitStmt(st, stmt, error)) return false;
  }
  return true;
}

bool EmitScopedBlock(EmitState& st, const std::vector<Stmt>& body, std::string* error) {
  auto scope = SaveLocalScope(st);
  if (!EmitBlock(st, body, error)) return false;
  RestoreLocalScope(st, std::move(scope));
  return true;
}

bool EmitIfChain(EmitState& st,
                 const std::vector<std::pair<Expr, std::vector<Stmt>>>& branches,
                 const std::vector<Stmt>& else_branch,
                 std::string* error) {
  std::string end_label = NewLabel(st, "if_end_");
  for (size_t i = 0; i < branches.size(); ++i) {
    const auto& branch = branches[i];
    std::string next_label = NewLabel(st, "if_next_");
    if (!EmitExpr(st, branch.first, nullptr, error)) return false;
    (*st.out) << "  jmp.false " << next_label << "\n";
    PopStack(st, 1);
    if (!EmitScopedBlock(st, branch.second, error)) return false;
    (*st.out) << "  jmp " << end_label << "\n";
    (*st.out) << next_label << ":\n";
  }
  if (!else_branch.empty()) {
    if (!EmitScopedBlock(st, else_branch, error)) return false;
  }
  (*st.out) << end_label << ":\n";
  return true;
}

bool EmitStmt(EmitState& st, const Stmt& stmt, std::string* error) {
  switch (stmt.kind) {
    case StmtKind::VarDecl: {
      const VarDecl& var = stmt.var_decl;
      if (!IsSupportedType(var.type)) {
        if (error) *error = "unsupported type for local '" + var.name + "'";
        return false;
      }
      if (st.local_indices.find(var.name) != st.local_indices.end()) {
        if (error) *error = "duplicate local '" + var.name + "'";
        return false;
      }
      uint16_t index = st.next_local++;
      st.local_indices[var.name] = index;
      TypeRef cloned;
      if (!CloneTypeRef(var.type, &cloned)) return false;
      st.local_types.emplace(var.name, std::move(cloned));
      if (var.has_init_expr) {
        std::string manifest_module;
        if (GetDlOpenManifestModule(var.init_expr, st, &manifest_module)) {
          st.local_dl_modules[var.name] = manifest_module;
        }
      }
      if (st.captured_locals.find(var.name) != st.captured_locals.end()) {
        return EmitCaptureCellCreate(
            st, var.name, index, var.type, var.has_init_expr ? &var.init_expr : nullptr, error);
      }
      if (var.has_init_expr) {
        if (!EmitExpr(st, var.init_expr, &var.type, error)) return false;
      } else {
        if (!EmitDefaultInit(st, var.type, error)) return false;
      }
      (*st.out) << "  stloc " << index << "\n";
      PopStack(st, 1);
      return true;
  }
    case StmtKind::Assign: {
      if (stmt.target.kind == ExprKind::Identifier) {
        auto type_it = st.local_types.find(stmt.target.text);
        if (type_it != st.local_types.end()) {
          return EmitLocalAssignment(st, stmt.target.text, type_it->second, stmt.expr, stmt.assign_op, false, error);
        }
        auto upvalue_it = st.current_upvalues.find(stmt.target.text);
        if (upvalue_it != st.current_upvalues.end()) {
          return EmitCapturedAssignment(
              st, stmt.target.text, upvalue_it->second.type, stmt.expr, stmt.assign_op, false, error);
        }
        auto gtype_it = st.global_types.find(stmt.target.text);
        if (gtype_it != st.global_types.end()) {
          return EmitGlobalAssignment(st, stmt.target.text, gtype_it->second, stmt.expr, stmt.assign_op, false, error);
        }
        if (error) *error = "unknown type for local '" + stmt.target.text + "'";
        return false;
      }
      if (stmt.target.kind == ExprKind::Index) {
        if (stmt.target.children.size() != 2) {
          if (error) *error = "index assignment expects target and index";
          return false;
        }
        TypeRef container_type;
        if (!InferExprType(stmt.target.children[0], st, &container_type, error)) return false;
        if (container_type.dims.empty()) {
          if (error) *error = "index assignment expects array or list target";
          return false;
        }
        TypeRef element_type;
        if (!CloneElementType(container_type, &element_type)) {
          if (error) *error = "failed to resolve index element type";
          return false;
        }
        const char* op_suffix = VmOpSuffixForType(element_type, st);
        if (!op_suffix) {
          if (error) *error = "unsupported index assignment element type for SIR emission";
          return false;
        }
        if (!EmitExpr(st, stmt.target.children[0], &container_type, error)) return false;
        TypeRef index_type;
        index_type.name = "i32";
        if (!EmitExpr(st, stmt.target.children[1], &index_type, error)) return false;
        if (stmt.assign_op != "=") {
          if (!EmitDup2(st)) return false;
          if (container_type.dims.front().is_list) {
            (*st.out) << "  list.get " << op_suffix << "\n";
          } else {
            (*st.out) << "  array.get " << op_suffix << "\n";
          }
          PopStack(st, 2);
          PushStack(st, 1);
          if (!EmitExpr(st, stmt.expr, &element_type, error)) return false;
          PopStack(st, 1);
          const char* bin_op = AssignOpToBinaryOp(stmt.assign_op);
          if (!bin_op) {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          const char* op_type = nullptr;
          if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
              std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
            op_type = NormalizeBitwiseOpType(element_type.name);
          } else {
            op_type = NormalizeNumericOpType(element_type.name);
          }
          if (!op_type) {
            if (error) *error = "unsupported operand type for '" + stmt.assign_op + "'";
            return false;
          }
          if (std::string(bin_op) == "+") {
            (*st.out) << "  add " << op_type << "\n";
          } else if (std::string(bin_op) == "-") {
            (*st.out) << "  sub " << op_type << "\n";
          } else if (std::string(bin_op) == "*") {
            (*st.out) << "  mul " << op_type << "\n";
          } else if (std::string(bin_op) == "/") {
            (*st.out) << "  div " << op_type << "\n";
          } else if (std::string(bin_op) == "%" && TAST::IsIntegerScalarTypeName(element_type.name)) {
            (*st.out) << "  mod " << op_type << "\n";
          } else if (std::string(bin_op) == "&") {
            (*st.out) << "  and " << op_type << "\n";
          } else if (std::string(bin_op) == "|") {
            (*st.out) << "  or " << op_type << "\n";
          } else if (std::string(bin_op) == "^") {
            (*st.out) << "  xor " << op_type << "\n";
          } else if (std::string(bin_op) == "<<") {
            (*st.out) << "  shl " << op_type << "\n";
          } else if (std::string(bin_op) == ">>") {
            (*st.out) << "  shr " << op_type << "\n";
          } else {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          if (container_type.dims.front().is_list) {
            (*st.out) << "  list.set " << op_suffix << "\n";
          } else {
            (*st.out) << "  array.set " << op_suffix << "\n";
          }
          PopStack(st, 3);
          return true;
        }
        if (!EmitExpr(st, stmt.expr, &element_type, error)) return false;
        if (container_type.dims.front().is_list) {
          (*st.out) << "  list.set " << op_suffix << "\n";
        } else {
          (*st.out) << "  array.set " << op_suffix << "\n";
        }
        PopStack(st, 3);
        return true;
      }
      if (stmt.target.kind == ExprKind::Member) {
        if (stmt.target.children.empty()) {
          if (error) *error = "member assignment missing base";
          return false;
        }
        const Expr& base = stmt.target.children[0];
        const bool is_ptr = (stmt.target.op == "->");
        if (base.kind == ExprKind::Identifier) {
          const std::string qualified = base.text + "." + stmt.target.text;
          auto gtype_it = st.global_types.find(qualified);
          if (gtype_it != st.global_types.end()) {
            return EmitGlobalAssignment(st, qualified, gtype_it->second, stmt.expr, stmt.assign_op, false, error);
          }
        }
        TypeRef base_type;
        if (!InferExprType(base, st, &base_type, error)) return false;
        if (is_ptr) {
          if (base_type.pointer_depth == 0) {
            if (error) *error = "pointer member assignment requires a pointer type";
            return false;
          }
          base_type.pointer_depth -= 1;
        }
        auto layout_it = st.artifact_layouts.find(base_type.name);
        if (layout_it == st.artifact_layouts.end()) {
          if (error) *error = "member assignment base is not an artifact";
          return false;
        }
        auto field_it = layout_it->second.field_index.find(stmt.target.text);
        if (field_it == layout_it->second.field_index.end()) {
          if (error) *error = "unknown field '" + stmt.target.text + "'";
          return false;
        }
        const TypeRef& field_type = layout_it->second.fields[field_it->second].type;
        if (!EmitExpr(st, base, &base_type, error)) return false;
        if (stmt.assign_op != "=") {
          if (!EmitDup(st)) return false;
          (*st.out) << "  ldfld " << base_type.name << "." << stmt.target.text << "\n";
          if (!EmitExpr(st, stmt.expr, &field_type, error)) return false;
          PopStack(st, 1);
          const char* bin_op = AssignOpToBinaryOp(stmt.assign_op);
          if (!bin_op) {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          const char* op_type = nullptr;
          if (std::string(bin_op) == "&" || std::string(bin_op) == "|" || std::string(bin_op) == "^" ||
              std::string(bin_op) == "<<" || std::string(bin_op) == ">>") {
            op_type = NormalizeBitwiseOpType(field_type.name);
          } else {
            op_type = NormalizeNumericOpType(field_type.name);
          }
          if (!op_type) {
            if (error) *error = "unsupported operand type for '" + stmt.assign_op + "'";
            return false;
          }
          if (std::string(bin_op) == "+") {
            (*st.out) << "  add " << op_type << "\n";
          } else if (std::string(bin_op) == "-") {
            (*st.out) << "  sub " << op_type << "\n";
          } else if (std::string(bin_op) == "*") {
            (*st.out) << "  mul " << op_type << "\n";
          } else if (std::string(bin_op) == "/") {
            (*st.out) << "  div " << op_type << "\n";
          } else if (std::string(bin_op) == "%" && TAST::IsIntegerScalarTypeName(field_type.name)) {
            (*st.out) << "  mod " << op_type << "\n";
          } else if (std::string(bin_op) == "&") {
            (*st.out) << "  and " << op_type << "\n";
          } else if (std::string(bin_op) == "|") {
            (*st.out) << "  or " << op_type << "\n";
          } else if (std::string(bin_op) == "^") {
            (*st.out) << "  xor " << op_type << "\n";
          } else if (std::string(bin_op) == "<<") {
            (*st.out) << "  shl " << op_type << "\n";
          } else if (std::string(bin_op) == ">>") {
            (*st.out) << "  shr " << op_type << "\n";
          } else {
            if (error) *error = "unsupported assignment operator '" + stmt.assign_op + "'";
            return false;
          }
          (*st.out) << "  stfld " << base_type.name << "." << stmt.target.text << "\n";
          PopStack(st, 2);
          return true;
        }
        if (!EmitExpr(st, stmt.expr, &field_type, error)) return false;
        (*st.out) << "  stfld " << base_type.name << "." << stmt.target.text << "\n";
        PopStack(st, 2);
        return true;
      }
      if (error) *error = "assignment target not supported in SIR emission";
      return false;
    }
    case StmtKind::Expr: {
      bool pop_result = true;
      TypeRef expr_type;
      if (InferExprType(stmt.expr, st, &expr_type, nullptr) && expr_type.name == "void") {
        pop_result = false;
      }
      TypeRef void_type;
      const TypeRef* expected = nullptr;
      if (IsDirectFnLiteralCall(stmt.expr)) {
        void_type.name = "void";
        expected = &void_type;
        pop_result = false;
      }
      if (!EmitExpr(st, stmt.expr, expected, error)) return false;
      if (pop_result) {
        (*st.out) << "  pop\n";
        PopStack(st, 1);
      }
      return true;
    }
    case StmtKind::Return: {
      if (stmt.has_return_expr) {
        const TypeRef* expected = nullptr;
        auto ret_it = st.func_returns.find(st.current_func);
        if (ret_it != st.func_returns.end() && ret_it->second.name != "void") {
          expected = &ret_it->second;
        }
        if (!EmitExpr(st, stmt.expr, expected, error)) return false;
      }
      (*st.out) << "  ret\n";
      st.stack_cur = 0;
      st.saw_return = true;
      return true;
    }
    case StmtKind::IfChain:
      return EmitIfChain(st, stmt.if_branches, stmt.else_branch, error);
    case StmtKind::IfStmt: {
      std::string else_label = NewLabel(st, "if_else_");
      std::string end_label = NewLabel(st, "if_end_");
      if (!EmitExpr(st, stmt.if_cond, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << else_label << "\n";
      PopStack(st, 1);
      if (!EmitScopedBlock(st, stmt.if_then, error)) return false;
      (*st.out) << "  jmp " << end_label << "\n";
      (*st.out) << else_label << ":\n";
      if (!stmt.if_else.empty()) {
        if (!EmitScopedBlock(st, stmt.if_else, error)) return false;
      }
      (*st.out) << end_label << ":\n";
      return true;
    }
    case StmtKind::WhileLoop: {
      std::string start_label = NewLabel(st, "while_start_");
      std::string end_label = NewLabel(st, "while_end_");
      st.loop_stack.push_back({end_label, start_label});
      (*st.out) << start_label << ":\n";
      if (!EmitExpr(st, stmt.loop_cond, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << end_label << "\n";
      PopStack(st, 1);
      if (!EmitScopedBlock(st, stmt.loop_body, error)) return false;
      (*st.out) << "  jmp " << start_label << "\n";
      (*st.out) << end_label << ":\n";
      st.loop_stack.pop_back();
      return true;
    }
    case StmtKind::ForLoop: {
      auto for_scope = SaveLocalScope(st);
      std::string start_label = NewLabel(st, "for_start_");
      std::string step_label = NewLabel(st, "for_step_");
      std::string end_label = NewLabel(st, "for_end_");
      if (stmt.has_loop_var_decl) {
        Stmt var_stmt;
        var_stmt.kind = StmtKind::VarDecl;
        var_stmt.var_decl = stmt.loop_var_decl;
        if (!EmitStmt(st, var_stmt, error)) return false;
      }
      if (!EmitExpr(st, stmt.loop_iter, nullptr, error)) return false;
      (*st.out) << "  pop\n";
      PopStack(st, 1);
      st.loop_stack.push_back({end_label, step_label});
      (*st.out) << start_label << ":\n";
      if (!EmitExpr(st, stmt.loop_cond, nullptr, error)) return false;
      (*st.out) << "  jmp.false " << end_label << "\n";
      PopStack(st, 1);
      if (!EmitScopedBlock(st, stmt.loop_body, error)) return false;
      (*st.out) << step_label << ":\n";
      if (!EmitExpr(st, stmt.loop_step, nullptr, error)) return false;
      (*st.out) << "  pop\n";
      PopStack(st, 1);
      (*st.out) << "  jmp " << start_label << "\n";
      (*st.out) << end_label << ":\n";
      st.loop_stack.pop_back();
      RestoreLocalScope(st, std::move(for_scope));
      return true;
    }
    case StmtKind::Break: {
      if (st.loop_stack.empty()) {
        if (error) *error = "break outside loop";
        return false;
      }
      (*st.out) << "  jmp " << st.loop_stack.back().break_label << "\n";
      return true;
    }
    case StmtKind::Skip: {
      if (st.loop_stack.empty()) {
        if (error) *error = "skip outside loop";
        return false;
      }
      (*st.out) << "  jmp " << st.loop_stack.back().continue_label << "\n";
      return true;
    }
    default:
      if (error) *error = "statement not supported for SIR emission";
      return false;
  }
}

bool EmitFunction(EmitState& st,
                  const FuncDecl& fn,
                  const std::string& emit_name,
                  const std::string& display_name,
                  const TypeRef* implicit_self,
                  bool is_entry,
                  const std::vector<Stmt>* script_body,
                  std::string* out,
                  std::string* error) {
  const std::vector<Stmt>& stmt_body = script_body ? *script_body : fn.body;
  if (!fn.generics.empty()) {
    if (error) *error = "generic functions not supported in SIR emission";
    return false;
  }
  if (!IsSupportedType(fn.return_type)) {
    if (error) *error = "unsupported return type for function '" + display_name + "'";
    return false;
  }
  st.current_func = emit_name;
  st.local_indices.clear();
  st.local_types.clear();
  st.local_dl_modules.clear();
  st.captured_locals.clear();
  st.current_upvalues.clear();
  std::unordered_set<std::string> available_capture_names;
  if (implicit_self) available_capture_names.insert("self");
  for (const auto& param : fn.params) available_capture_names.insert(param.name);
  IRE::CollectAllLocalNames(stmt_body, &available_capture_names);
  IRE::CollectCapturedLocalsFromStatements(
      stmt_body, available_capture_names, &st.captured_locals);
  auto lambda_capture_it = st.lambda_captures.find(emit_name);
  if (lambda_capture_it != st.lambda_captures.end()) {
    for (size_t i = 0; i < lambda_capture_it->second.size(); ++i) {
      EmitState::CaptureInfo info;
      if (!CloneTypeRef(lambda_capture_it->second[i].second, &info.type)) return false;
      info.index = static_cast<uint16_t>(i);
      st.current_upvalues.emplace(lambda_capture_it->second[i].first, std::move(info));
    }
  }
  st.next_local = 0;
  st.stack_cur = 0;
  st.stack_max = 0;
  st.saw_return = false;
  st.label_counter = 0;
  st.loop_stack.clear();
  uint16_t locals_count = 0;
  for (const auto& stmt : stmt_body) {
    if (stmt.kind == StmtKind::VarDecl) locals_count++;
  }
  uint16_t param_count = static_cast<uint16_t>(fn.params.size());
  if (implicit_self) {
    param_count = static_cast<uint16_t>(param_count + 1);
  }
  uint16_t total_locals = static_cast<uint16_t>(locals_count + param_count);
  std::ostringstream func_out;
  st.out = &func_out;

  (*st.out) << "func " << emit_name << " locals=" << total_locals << " stack=0 sig=" << emit_name << "\n";
  if (lambda_capture_it != st.lambda_captures.end()) {
    for (size_t i = 0; i < lambda_capture_it->second.size(); ++i) {
      (*st.out) << "  upvalue " << lambda_capture_it->second[i].first
                << " ref " << i << "\n";
    }
  }
  (*st.out) << "  enter " << total_locals << "\n";

  if (implicit_self) {
    uint16_t index = st.next_local++;
    st.local_indices.emplace("self", index);
    TypeRef cloned;
    if (!CloneTypeRef(*implicit_self, &cloned)) return false;
    st.local_types.emplace("self", std::move(cloned));
  }

  for (const auto& param : fn.params) {
    uint16_t index = st.next_local++;
    st.local_indices.emplace(param.name, index);
    TypeRef cloned;
    if (!CloneTypeRef(param.type, &cloned)) return false;
    st.local_types.emplace(param.name, std::move(cloned));
  }

  std::vector<std::string> ordered_captured_locals(
      st.captured_locals.begin(), st.captured_locals.end());
  std::sort(ordered_captured_locals.begin(), ordered_captured_locals.end());
  for (const auto& name : ordered_captured_locals) {
    auto local_it = st.local_indices.find(name);
    auto type_it = st.local_types.find(name);
    if (local_it != st.local_indices.end() && type_it != st.local_types.end()) {
      const uint16_t source_index = local_it->second;
      const uint16_t cell_index = st.next_local++;
      if (!EmitCaptureCellBoxExistingLocal(
              st, name, source_index, cell_index, type_it->second, error)) {
        return false;
      }
      st.local_indices[name] = cell_index;
    }
  }

  if (!st.global_init_func_name.empty() &&
      is_entry &&
      emit_name != st.global_init_func_name) {
    auto init_it = st.func_ids.find(st.global_init_func_name);
    if (init_it == st.func_ids.end()) {
      if (error) *error = "missing global init function id";
      return false;
    }
    (*st.out) << "  call " << init_it->second << " 0\n";
  }

  if (!st.global_init_func_name.empty() && emit_name == st.global_init_func_name) {
    for (const auto* glob : st.global_decls) {
      if (!glob->has_init_expr && !NeedsRuntimeDefaultInitialization(st, glob->type)) {
        continue;
      }
      if (glob->has_init_expr) {
        if (!EmitExpr(st, glob->init_expr, &glob->type, error)) return false;
      } else {
        if (!EmitDefaultInit(st, glob->type, error)) return false;
      }
      auto git = st.global_indices.find(glob->name);
      if (git == st.global_indices.end()) {
        if (error) *error = "unknown global in init function '" + glob->name + "'";
        return false;
      }
      (*st.out) << "  stglob " << git->second << "\n";
      PopStack(st, 1);
    }
  }

  for (const auto& stmt : stmt_body) {
    if (!EmitStmt(st, stmt, error)) {
      if (error && !error->empty()) {
        *error = "in function '" + display_name + "': " + *error;
      }
      return false;
    }
  }

  const bool return_is_void = (fn.return_type.name == "void");
  const bool body_returns = Simple::Lang::TAST::AnalyzeBlockFlow(stmt_body).always_returns;
  if (return_is_void || !body_returns) {
    if (!body_returns && (fn.name == "main" || is_entry) && fn.return_type.name == "i32") {
      (*st.out) << "  const i32 0\n";
      PushStack(st, 1);
    }
    (*st.out) << "  ret\n";
  }

  std::string func_body = func_out.str();
  st.out = nullptr;

  size_t header_end = func_body.find('\n');
  std::string header = func_body.substr(0, header_end);
  std::string body_text = func_body.substr(header_end + 1);
  total_locals = st.next_local;
  const size_t enter_start = body_text.find("  enter ");
  const size_t enter_end = enter_start == std::string::npos
                               ? std::string::npos
                               : body_text.find('\n', enter_start);
  if (enter_end != std::string::npos) {
    body_text.replace(
        enter_start, enter_end - enter_start, "  enter " + std::to_string(total_locals));
  }

  header = "func " + emit_name +
           " locals=" + std::to_string(total_locals) +
           " stack=" + std::to_string(st.stack_max > 0 ? st.stack_max : 8) +
           " sig=" + emit_name;

  func_out.str(std::string());
  func_out.clear();
  func_out << header << "\n" << body_text << "end\n";
  st.out = nullptr;
  func_body = func_out.str();
  if (out) *out = func_body;
  return true;
}

bool EmitProgramImpl(const Program& program, std::string* out, std::string* error) {
  EmitState st;
  st.error = error;

  std::vector<FuncItem> functions;
  std::vector<const ArtifactDecl*> artifacts;
  std::vector<const EnumDecl*> enums;
  std::vector<const ExternDecl*> externs;
  std::vector<const VarDecl*> globals;
  std::vector<VarDecl> module_globals;
  size_t module_var_count = 0;
  FuncDecl global_init_fn;
  FuncDecl script_entry_fn;
  const bool has_top_level_script = !program.top_level_stmts.empty();
  bool has_main = false;
  std::string main_emit_name;
  for (const auto& decl : program.decls) {
    if (decl.kind == DeclKind::Module) {
      module_var_count += decl.module.variables.size();
    }
  }

  for (const auto* ext : externs) {
    if (NeedsAbiFlattenType(ext->return_type, st)) {
      if (!EnsureAbiTypeForArtifact(st, ext->return_type.name, nullptr, error)) return false;
    }
    for (const auto& param : ext->params) {
      if (NeedsAbiFlattenType(param.type, st)) {
        if (!EnsureAbiTypeForArtifact(st, param.type.name, nullptr, error)) return false;
      }
    }
  }
  if (module_var_count > 0) {
    module_globals.reserve(module_var_count);
  }
  for (const auto& decl : program.decls) {
    if (decl.kind == DeclKind::ModuleHeader) {
      continue;
    }
    if (decl.kind == DeclKind::Import || decl.kind == DeclKind::Extern) {
      if (decl.kind == DeclKind::Import) {
        if (decl.import_decl.is_using) {
          const auto alias_it = st.reserved_import_aliases.find(decl.import_decl.path);
          if (alias_it != st.reserved_import_aliases.end()) {
            st.reserved_imports.insert(alias_it->second);
            st.using_reserved_modules.insert(std::string(ToCanonicalName(alias_it->second)));
          } else if (st.extern_ids_by_module.find(decl.import_decl.path) != st.extern_ids_by_module.end()) {
            st.using_modules.insert(decl.import_decl.path);
          } else if (st.imported_modules.find(decl.import_decl.path) != st.imported_modules.end()) {
            // Imported file space is already flattened into this program.
          } else {
            const size_t dot = decl.import_decl.path.rfind('.');
            st.using_modules.insert(dot == std::string::npos ? decl.import_decl.path : decl.import_decl.path.substr(dot + 1));
          }
        } else {
          const auto library_import = ParseLibraryImportPath(decl.import_decl.path);
          if (!library_import) {
            st.imported_modules.insert(decl.import_decl.path);
            continue;
          }
          LibraryModuleId module_id{library_import->root, library_import->module_index};
          st.reserved_imports.insert(module_id);
          if (decl.import_decl.has_alias && !decl.import_decl.alias.empty()) {
            st.reserved_import_aliases[decl.import_decl.alias] = module_id;
          } else {
            st.reserved_import_aliases[decl.import_decl.path] = module_id;
          }
        }
      }
      if (decl.kind == DeclKind::Extern) {
        externs.push_back(&decl.ext);
      }
      continue;
    } else if (decl.kind == DeclKind::Function) {
      functions.push_back({&decl.func, decl.func.name, decl.func.name, false, {}});
      if (decl.func.name == "main" &&
          decl.func.return_type.name == "i32" &&
          decl.func.params.empty()) {
        has_main = true;
        main_emit_name = decl.func.name;
      }
    } else if (decl.kind == DeclKind::Artifact) {
      artifacts.push_back(&decl.artifact);
      st.artifacts.emplace(decl.artifact.name, &decl.artifact);
      for (const auto& method : decl.artifact.methods) {
        const std::string emit_name = decl.artifact.name + "__" + method.name;
        const std::string display = decl.artifact.name + "." + method.name;
        st.artifact_method_names.emplace(display, emit_name);
        FuncItem item;
        item.decl = &method;
        item.emit_name = emit_name;
        item.display_name = display;
        item.has_self = true;
        item.self_type.name = decl.artifact.name;
        functions.push_back(std::move(item));
      }
    } else if (decl.kind == DeclKind::Enum) {
      enums.push_back(&decl.enm);
      std::unordered_map<std::string, int64_t> values;
      for (const auto& member : decl.enm.members) {
        int64_t value = 0;
        if (member.has_value) {
          if (!ParseIntegerLiteralText(member.value_text, &value)) {
            if (error) *error = "invalid enum value for " + decl.enm.name + "." + member.name;
            return false;
          }
        }
        values.emplace(member.name, value);
      }
      st.enum_values.emplace(decl.enm.name, std::move(values));
    } else if (decl.kind == DeclKind::Module) {
      if (!decl.module.source_module.empty()) {
        const std::string qualified_module = decl.module.source_module + "." + decl.module.name;
        if (qualified_module != decl.module.name) st.module_aliases[qualified_module] = decl.module.name;
      }
      if (!decl.module.variables.empty()) {
        for (const auto& var : decl.module.variables) {
          VarDecl qualified = var;
          qualified.name = decl.module.name + "." + var.name;
          module_globals.push_back(std::move(qualified));
          globals.push_back(&module_globals.back());
        }
      }
      for (const auto& fn : decl.module.functions) {
        const std::string key = decl.module.name + "." + fn.name;
        const std::string emit_name = decl.module.name + "__" + fn.name;
        st.module_func_names.emplace(key, emit_name);
        functions.push_back({&fn, emit_name, key, false, {}});
      }
      for (const auto& ext : decl.module.externs) {
        externs.push_back(&ext);
      }
    } else if (decl.kind == DeclKind::Variable) {
      globals.push_back(&decl.var);
    } else {
      if (error) *error = "unsupported top-level declaration in SIR emission";
      return false;
    }
  }
  if (!globals.empty()) {
    st.global_decls = globals;
    bool has_global_init = false;
    for (const auto* g : globals) {
      if (g->has_init_expr || NeedsRuntimeDefaultInitialization(st, g->type)) {
        has_global_init = true;
        break;
      }
    }
    if (has_global_init) {
      global_init_fn.name = "__global_init";
      global_init_fn.return_type.name = "void";
      global_init_fn.return_mutability = Mutability::Mutable;
      st.global_init_func_name = global_init_fn.name;
      functions.push_back({&global_init_fn, global_init_fn.name, global_init_fn.name, false, {}});
    }
  }
  if (has_top_level_script && !has_main) {
    script_entry_fn.name = "__script_entry";
    script_entry_fn.return_mutability = Mutability::Mutable;
    script_entry_fn.return_type.name = "i32";
    FuncItem item;
    item.decl = &script_entry_fn;
    item.emit_name = script_entry_fn.name;
    item.display_name = script_entry_fn.name;
    item.has_self = false;
    item.script_body = &program.top_level_stmts;
    functions.push_back(std::move(item));
  }
  if (functions.empty()) {
    if (error) *error = "program has no functions or top-level statements";
    return false;
  }

  for (const auto* glob : globals) {
    TypeRef gtype;
    if (!CloneTypeRef(glob->type, &gtype)) return false;
    uint32_t index = static_cast<uint32_t>(st.global_indices.size());
    st.global_indices[glob->name] = index;
    st.global_types[glob->name] = std::move(gtype);
    st.global_mutability[glob->name] = glob->mutability;
  }

  for (size_t i = 0; i < functions.size(); ++i) {
    st.func_ids[functions[i].emit_name] = static_cast<uint32_t>(i);
    TypeRef ret;
    if (!CloneTypeRef(functions[i].decl->return_type, &ret)) return false;
    st.func_returns.emplace(functions[i].emit_name, std::move(ret));
    if (functions[i].decl->is_async) st.async_funcs.insert(functions[i].emit_name);
    std::vector<TypeRef> params;
    params.reserve(functions[i].decl->params.size() + (functions[i].has_self ? 1u : 0u));
    if (functions[i].has_self) {
      TypeRef cloned;
      if (!CloneTypeRef(functions[i].self_type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    for (const auto& param : functions[i].decl->params) {
      TypeRef cloned;
      if (!CloneTypeRef(param.type, &cloned)) return false;
      params.push_back(std::move(cloned));
    }
    st.func_params.emplace(functions[i].emit_name, std::move(params));
  }
  st.base_func_count = static_cast<uint32_t>(functions.size());

  std::unordered_map<std::string, size_t> import_index_by_key;
  auto clone_params = [&](const std::vector<TypeRef>& src, std::vector<TypeRef>* out_params) -> bool {
    if (!out_params) return false;
    out_params->clear();
    out_params->reserve(src.size());
    for (const auto& param : src) {
      TypeRef cloned;
      if (!CloneTypeRef(param, &cloned)) return false;
      out_params->push_back(std::move(cloned));
    }
    return true;
  };
  uint32_t dynamic_dl_call_index = 0;
  for (const auto* ext : externs) {
    std::string module = ext->has_module ? ResolveImportModule(ext->module) : std::string("host");
    std::string symbol = ext->name;
    std::string key = module + '\0' + symbol;
    if (import_index_by_key.find(key) != import_index_by_key.end()) {
      if (error) *error = "duplicate extern import: " + (module.empty() ? symbol : (module + "." + symbol));
      return false;
    }
    EmitState::ImportItem item;
    item.name = "import_" + std::to_string(st.imports.size());
    item.module = module;
    item.symbol = symbol;
    item.sig_name = "sig_import_" + std::to_string(st.imports.size());
    item.flags = 0;
    std::vector<TypeRef> abi_params;
    abi_params.reserve(ext->params.size());
    for (const auto& param : ext->params) {
      if (!IsSupportedDlAbiType(param.type, st, false)) {
        if (error) {
          *error = "extern '" + (ext->has_module ? (ext->module + ".") : std::string()) + ext->name +
                   "' parameter '" + param.name + "' has unsupported ABI type";
        }
        return false;
      }
      TypeRef cloned_param;
      if (NeedsAbiFlattenType(param.type, st)) {
        std::string abi_name;
        if (!EnsureAbiTypeForArtifact(st, param.type.name, &abi_name, error)) return false;
        cloned_param.name = abi_name;
        cloned_param.pointer_depth = 0;
        cloned_param.is_proc = false;
        cloned_param.type_args.clear();
        cloned_param.dims.clear();
        cloned_param.proc_params.clear();
        cloned_param.proc_return.reset();
      } else {
        if (!CloneTypeRef(param.type, &cloned_param)) return false;
      }
      abi_params.push_back(std::move(cloned_param));
    }
    if (!IsSupportedDlAbiType(ext->return_type, st, true)) {
      if (error) {
        *error = "extern '" + (ext->has_module ? (ext->module + ".") : std::string()) + ext->name +
                 "' return has unsupported ABI type";
      }
      return false;
    }
    TypeRef abi_ret;
    if (NeedsAbiFlattenType(ext->return_type, st)) {
      std::string abi_name;
      if (!EnsureAbiTypeForArtifact(st, ext->return_type.name, &abi_name, error)) return false;
      abi_ret.name = abi_name;
      abi_ret.pointer_depth = 0;
      abi_ret.is_proc = false;
      abi_ret.type_args.clear();
      abi_ret.dims.clear();
      abi_ret.proc_params.clear();
      abi_ret.proc_return.reset();
    } else {
      if (!CloneTypeRef(ext->return_type, &abi_ret)) return false;
    }
    item.params = std::move(abi_params);
    item.ret = std::move(abi_ret);
    import_index_by_key.emplace(key, st.imports.size());
    st.imports.push_back(std::move(item));

    std::vector<TypeRef> param_copy;
    param_copy.reserve(ext->params.size());
    for (const auto& param : ext->params) {
      TypeRef cloned;
      if (!CloneTypeRef(param.type, &cloned)) return false;
      param_copy.push_back(std::move(cloned));
    }
    TypeRef ret_copy;
    if (!CloneTypeRef(ext->return_type, &ret_copy)) return false;
    if (ext->has_module) {
      st.extern_ids_by_module[ext->module][symbol] = st.imports.back().name;
      st.extern_params_by_module[ext->module][symbol] = std::move(param_copy);
      st.extern_returns_by_module[ext->module][symbol] = std::move(ret_copy);
    } else {
      st.extern_ids[symbol] = st.imports.back().name;
      st.extern_params[symbol] = std::move(param_copy);
      st.extern_returns[symbol] = std::move(ret_copy);
    }

    if (ext->has_module &&
        !IsCanonicalLibraryModule(ResolveImportModule(ext->module), SystemModule::FFI) &&
        IsSupportedDlAbiType(st.imports.back().ret, st, true)) {
      bool all_params_scalar = true;
      for (const auto& p : st.imports.back().params) {
        if (!IsSupportedDlAbiType(p, st, false)) {
          all_params_scalar = false;
          break;
        }
      }
      if (all_params_scalar) {
        EmitState::ImportItem dyn_item;
        dyn_item.name = "import_" + std::to_string(st.imports.size());
        dyn_item.module = "System.FFI";
        dyn_item.symbol = "call$" + std::to_string(dynamic_dl_call_index++);
        dyn_item.sig_name = "sig_import_" + std::to_string(st.imports.size());
        dyn_item.flags = 0;
        TypeRef ptr_type;
        ptr_type.name = "i64";
        dyn_item.params.push_back(std::move(ptr_type));
        for (const auto& param : st.imports.back().params) {
          TypeRef cloned_param;
          if (!CloneTypeRef(param, &cloned_param)) return false;
          dyn_item.params.push_back(std::move(cloned_param));
        }
        if (!CloneTypeRef(st.imports.back().ret, &dyn_item.ret)) return false;
        st.dl_call_import_ids_by_module[ext->module][symbol] = dyn_item.name;
        st.imports.push_back(std::move(dyn_item));
      }
    }
  }

  for (const auto& alias_entry : st.module_aliases) {
    const std::string& alias = alias_entry.first;
    const std::string& target = alias_entry.second;
    auto ids_it = st.extern_ids_by_module.find(target);
    if (ids_it != st.extern_ids_by_module.end()) st.extern_ids_by_module[alias] = ids_it->second;
    auto params_it = st.extern_params_by_module.find(target);
    if (params_it != st.extern_params_by_module.end()) st.extern_params_by_module[alias] = params_it->second;
    auto returns_it = st.extern_returns_by_module.find(target);
    if (returns_it != st.extern_returns_by_module.end()) st.extern_returns_by_module[alias] = returns_it->second;
    auto dl_it = st.dl_call_import_ids_by_module.find(target);
    if (dl_it != st.dl_call_import_ids_by_module.end()) st.dl_call_import_ids_by_module[alias] = dl_it->second;
  }

  for (const auto* glob : globals) {
    if (!glob->has_init_expr) continue;
    std::string manifest_module;
    if (GetDlOpenManifestModule(glob->init_expr, st, &manifest_module)) {
      st.global_dl_modules[glob->name] = manifest_module;
    }
  }

  auto make_type = [](const char* name) {
    TypeRef out;
    out.name = name;
    out.type_args.clear();
    out.dims.clear();
    out.is_proc = false;
    out.proc_params.clear();
    out.proc_return.reset();
    return out;
  };
  auto make_list_type = [&](const char* name) {
    TypeRef out = make_type(name);
    TypeDim dim;
    dim.is_list = true;
    dim.has_size = false;
    dim.size = 0;
    out.dims.push_back(dim);
    return out;
  };
  auto add_reserved_import = [&](const std::string& module_alias,
                                 const std::string& module,
                                 const std::string& symbol,
                                 std::vector<TypeRef>&& params,
                                 TypeRef&& ret) -> bool {
    std::string key = module + '\0' + symbol;
    auto existing_it = import_index_by_key.find(key);
    if (existing_it != import_index_by_key.end()) {
      const size_t existing_idx = existing_it->second;
      st.extern_ids_by_module[module_alias][symbol] = st.imports[existing_idx].name;
      std::vector<TypeRef> param_copy;
      if (!clone_params(st.imports[existing_idx].params, &param_copy)) return false;
      TypeRef ret_copy;
      if (!CloneTypeRef(st.imports[existing_idx].ret, &ret_copy)) return false;
      st.extern_params_by_module[module_alias][symbol] = std::move(param_copy);
      st.extern_returns_by_module[module_alias][symbol] = std::move(ret_copy);
      return true;
    }
    EmitState::ImportItem item;
    item.name = "import_" + std::to_string(st.imports.size());
    item.module = module;
    item.symbol = symbol;
    item.sig_name = "sig_import_" + std::to_string(st.imports.size());
    item.flags = 0;
    item.params = std::move(params);
    item.ret = std::move(ret);
    import_index_by_key.emplace(key, st.imports.size());
    st.imports.push_back(std::move(item));
    std::vector<TypeRef> param_copy;
    if (!clone_params(st.imports.back().params, &param_copy)) return false;
    TypeRef ret_copy;
    if (!CloneTypeRef(st.imports.back().ret, &ret_copy)) return false;
    st.extern_ids_by_module[module_alias][symbol] = st.imports.back().name;
    st.extern_params_by_module[module_alias][symbol] = std::move(param_copy);
    st.extern_returns_by_module[module_alias][symbol] = std::move(ret_copy);
    return true;
  };

  auto native_type_to_lang_type = [&](Simple::Byte::TypeKind kind, TypeRef* out) -> bool {
    if (!out) return false;
    switch (kind) {
      case Simple::Byte::TypeKind::Bool:
        *out = make_type("bool");
        return true;
      case Simple::Byte::TypeKind::I32:
        *out = make_type("i32");
        return true;
      case Simple::Byte::TypeKind::I64:
        *out = make_type("i64");
        return true;
      case Simple::Byte::TypeKind::F32:
        *out = make_type("f32");
        return true;
      case Simple::Byte::TypeKind::F64:
        *out = make_type("f64");
        return true;
      case Simple::Byte::TypeKind::String:
        *out = make_type("string");
        return true;
      case Simple::Byte::TypeKind::Ref:
        *out = make_list_type("i32");
        return true;
      case Simple::Byte::TypeKind::Unspecified:
        *out = make_type("void");
        return true;
      default:
        return false;
    }
  };
  auto library_type_to_lang_type = [&](const LibraryTypeSpec& spec, TypeRef* out) -> bool {
    if (!out) return false;
    std::string name(spec.name);
    if (name.size() >= 2 && name.substr(name.size() - 2) == "[]") {
      name.resize(name.size() - 2);
      *out = make_list_type(name.c_str());
      return true;
    }
    *out = make_type(name.c_str());
    return true;
  };
  auto native_module_for_reserved = [](const std::string& reserved, std::string* out) -> bool {
    if (!out) return false;
    const auto module = ParseCanonicalLibraryModule(reserved);
    if (!module) return false;
    const std::string_view native = ToNativeModule(*module);
    if (native.empty()) return false;
    *out = std::string(native);
    return true;
  };
  auto add_native_reserved_imports = [&](const std::string& reserved,
                                         const std::vector<std::string>& aliases) -> bool {
    std::string native_module;
    if (!native_module_for_reserved(reserved, &native_module)) return true;
    static const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
    for (const auto& alias : aliases) {
      for (const auto& spec : registry.Functions()) {
        if (spec.module_name != native_module) continue;
        std::vector<TypeRef> params;
        params.reserve(spec.parameter_types.size());
        for (Simple::Byte::TypeKind kind : spec.parameter_types) {
          TypeRef param;
          if (!native_type_to_lang_type(kind, &param)) return false;
          params.push_back(std::move(param));
        }
        TypeRef ret;
        if (!native_type_to_lang_type(spec.result_type, &ret)) return false;
        if (!add_reserved_import(alias, spec.module_name, spec.symbol_name, std::move(params), std::move(ret))) {
          return false;
        }
      }
    }
    return true;
  };

  auto add_catalog_reserved_imports = [&](LibraryModuleId module,
                                          const std::vector<std::string>& aliases) -> bool {
    const std::string_view native_module = ToNativeModule(module);
    if (native_module.empty()) return true;
    for (const std::string_view member : MemberNames(module)) {
      const auto signature = GetLibrarySignature(module, member);
      if (!signature) continue;
      std::vector<TypeRef> params;
      params.reserve(signature->params.size());
      for (const LibraryParamSpec& param : signature->params) {
        TypeRef param_type;
        if (!library_type_to_lang_type(param.type, &param_type)) return false;
        params.push_back(std::move(param_type));
      }
      TypeRef ret;
      if (!library_type_to_lang_type(signature->return_type, &ret)) return false;
      for (const auto& alias : aliases) {
        std::vector<TypeRef> params_copy;
        if (!clone_params(params, &params_copy)) return false;
        TypeRef ret_copy;
        if (!CloneTypeRef(ret, &ret_copy)) return false;
        if (!add_reserved_import(alias,
                                 std::string(native_module),
                                 std::string(member),
                                 std::move(params_copy),
                                 std::move(ret_copy))) {
          return false;
        }
      }
    }
    return true;
  };

  auto module_id_for_name = [](const std::string& name) -> std::optional<LibraryModuleId> {
    if (auto module = ParseCanonicalLibraryModule(name)) return module;
    return std::nullopt;
  };
  auto reserved_aliases_for = [&](const std::string& name) {
    std::vector<std::string> aliases;
    aliases.push_back(name);
    const auto module = module_id_for_name(name);
    for (const auto& entry : st.reserved_import_aliases) {
      if (module && entry.second == *module) aliases.push_back(entry.first);
    }
    return aliases;
  };
  auto system_id = [](SystemModule module) {
    return LibraryModuleId{LibraryRoot::System, static_cast<int>(module)};
  };
  auto standard_id = [](StandardModule module) {
    return LibraryModuleId{LibraryRoot::Standard, static_cast<int>(module)};
  };
  auto has_reserved_module = [&](LibraryModuleId module) {
    return st.reserved_imports.find(module) != st.reserved_imports.end();
  };
  auto reserved_aliases_for_id = [&](LibraryModuleId module) {
    std::vector<std::string> aliases;
    aliases.emplace_back(ToCanonicalName(module));
    for (const auto& entry : st.reserved_import_aliases) {
      if (entry.second == module) aliases.push_back(entry.first);
    }
    return aliases;
  };

  if (has_reserved_module(system_id(SystemModule::FFI))) {
    for (const auto& alias : reserved_aliases_for_id(system_id(SystemModule::FFI))) {
      std::vector<TypeRef> open_params;
      open_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FFI", "open", std::move(open_params), make_type("i64"))) return false;

      std::vector<TypeRef> sym_params;
      sym_params.push_back(make_type("i64"));
      sym_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FFI", "sym", std::move(sym_params), make_type("i64"))) return false;

      std::vector<TypeRef> close_params;
      close_params.push_back(make_type("i64"));
      if (!add_reserved_import(alias, "System.FFI", "close", std::move(close_params), make_type("i32"))) return false;

      if (!add_reserved_import(alias, "System.FFI", "lastError", {}, make_type("string"))) return false;
      if (!add_reserved_import(alias, "System.FFI", "last_error", {}, make_type("string"))) return false;
    }
  }

  if (has_reserved_module(standard_id(StandardModule::Time))) {
    for (const auto& alias : reserved_aliases_for_id(standard_id(StandardModule::Time))) {
      std::vector<TypeRef> format_params;
      format_params.push_back(make_type("i64"));
      if (!add_reserved_import(alias, "System.OS", "formatWallNs", std::move(format_params), make_type("string"))) return false;
    }
  }

  if (has_reserved_module(system_id(SystemModule::OS)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::OS),
                                    reserved_aliases_for_id(system_id(SystemModule::OS)))) return false;

  auto add_fs_imports = [&](const std::string& canonical_module, bool include_handles) {
    for (const auto& alias : reserved_aliases_for(canonical_module)) {
      std::vector<TypeRef> read_text_params;
      read_text_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FS", "readText", std::move(read_text_params), make_type("string"))) return false;
      std::vector<TypeRef> write_text_params;
      write_text_params.push_back(make_type("string"));
      write_text_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FS", "writeText", std::move(write_text_params), make_type("bool"))) return false;
      std::vector<TypeRef> read_bytes_params;
      read_bytes_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FS", "readBytes", std::move(read_bytes_params), make_list_type("i32"))) return false;
      std::vector<TypeRef> write_bytes_params;
      write_bytes_params.push_back(make_type("string"));
      write_bytes_params.push_back(make_list_type("i32"));
      if (!add_reserved_import(alias, "System.FS", "writeBytes", std::move(write_bytes_params), make_type("bool"))) return false;
      std::vector<TypeRef> list_dir_params;
      list_dir_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FS", "listDir", std::move(list_dir_params), make_list_type("string"))) return false;
      for (const std::string member : {"exists", "isFile", "isDir"}) {
        std::vector<TypeRef> params;
        params.push_back(make_type("string"));
        if (!add_reserved_import(alias, "System.Path", member, std::move(params), make_type("bool"))) return false;
      }
      if (include_handles) {
        std::vector<TypeRef> open_params;
        open_params.push_back(make_type("string"));
        open_params.push_back(make_type("i32"));
        if (!add_reserved_import(alias, "System.FS", "open", std::move(open_params), make_type("i32"))) return false;
        std::vector<TypeRef> close_params;
        close_params.push_back(make_type("i32"));
        if (!add_reserved_import(alias, "System.FS", "close", std::move(close_params), make_type("void"))) return false;
        auto make_rw_params = [&]() {
          std::vector<TypeRef> params;
          params.push_back(make_type("i32"));
          params.push_back(make_list_type("i32"));
          params.push_back(make_type("i32"));
          return params;
        };
        if (!add_reserved_import(alias, "System.FS", "read", make_rw_params(), make_type("i32"))) return false;
        if (!add_reserved_import(alias, "System.FS", "write", make_rw_params(), make_type("i32"))) return false;
      }
      std::vector<TypeRef> copy_params;
      copy_params.push_back(make_type("string"));
      copy_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.FS", "copy", std::move(copy_params), make_type("bool"))) return false;
      for (const std::string member : {"remove", "mkdir", "mkdirAll", "setCwd"}) {
        std::vector<TypeRef> params;
        params.push_back(make_type("string"));
        if (!add_reserved_import(alias, "System.FS", member, std::move(params), make_type("bool"))) return false;
      }
      if (!add_reserved_import(alias, "System.FS", "cwd", {}, make_type("string"))) return false;
    }
    return true;
  };
  if (has_reserved_module(system_id(SystemModule::FS)) &&
      !add_fs_imports(std::string(ToCanonicalName(system_id(SystemModule::FS))), true)) return false;
  if (has_reserved_module(standard_id(StandardModule::FS)) &&
      !add_fs_imports(std::string(ToCanonicalName(standard_id(StandardModule::FS))), false)) return false;

  auto add_path_imports = [&](const std::string& canonical_module, bool low_level) {
    for (const auto& alias : reserved_aliases_for(canonical_module)) {
      if (low_level) {
        if (!add_reserved_import(alias, "System.Path", "separator", {}, make_type("string"))) return false;
        if (!add_reserved_import(alias, "System.Path", "delimiter", {}, make_type("string"))) return false;
        std::vector<TypeRef> absolute_params;
        absolute_params.push_back(make_type("string"));
        if (!add_reserved_import(alias, "System.Path", "isAbsolute", std::move(absolute_params), make_type("bool"))) return false;
      }
      std::vector<TypeRef> join_params;
      join_params.push_back(make_type("string"));
      join_params.push_back(make_type("string"));
      if (!add_reserved_import(alias, "System.Path", "join", std::move(join_params), make_type("string"))) return false;
      for (const std::string member : {"dirname", "basename", "ext", "stem", "normalize"}) {
        std::vector<TypeRef> params;
        params.push_back(make_type("string"));
        if (!add_reserved_import(alias, "System.Path", member, std::move(params), make_type("string"))) return false;
      }

    }
    return true;
  };
  if (has_reserved_module(system_id(SystemModule::Path)) &&
      !add_path_imports(std::string(ToCanonicalName(system_id(SystemModule::Path))), true)) return false;
  if (has_reserved_module(standard_id(StandardModule::Path)) &&
      !add_path_imports(std::string(ToCanonicalName(standard_id(StandardModule::Path))), false)) return false;

  if (has_reserved_module(system_id(SystemModule::Env)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Env),
                                    reserved_aliases_for_id(system_id(SystemModule::Env)))) return false;

  if (has_reserved_module(system_id(SystemModule::Random)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Random),
                                    reserved_aliases_for_id(system_id(SystemModule::Random)))) return false;
  if (has_reserved_module(standard_id(StandardModule::Random)) &&
      !add_catalog_reserved_imports(standard_id(StandardModule::Random),
                                    reserved_aliases_for_id(standard_id(StandardModule::Random)))) return false;

  if (has_reserved_module(system_id(SystemModule::Channel)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Channel),
                                    reserved_aliases_for_id(system_id(SystemModule::Channel)))) return false;

  if (has_reserved_module(system_id(SystemModule::Job)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Job),
                                    reserved_aliases_for_id(system_id(SystemModule::Job)))) return false;
  if (has_reserved_module(standard_id(StandardModule::Promise)) &&
      !add_catalog_reserved_imports(standard_id(StandardModule::Promise),
                                    reserved_aliases_for_id(standard_id(StandardModule::Promise)))) return false;

  if (has_reserved_module(system_id(SystemModule::Process)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Process),
                                    reserved_aliases_for_id(system_id(SystemModule::Process)))) return false;
  if (has_reserved_module(standard_id(StandardModule::Process)) &&
      !add_catalog_reserved_imports(standard_id(StandardModule::Process),
                                    reserved_aliases_for_id(standard_id(StandardModule::Process)))) return false;

  if (has_reserved_module(system_id(SystemModule::Thread)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Thread),
                                    reserved_aliases_for_id(system_id(SystemModule::Thread)))) return false;

  if (has_reserved_module(system_id(SystemModule::IO)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::IO),
                                    reserved_aliases_for_id(system_id(SystemModule::IO)))) return false;

  if (has_reserved_module(system_id(SystemModule::Json)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Json),
                                    reserved_aliases_for_id(system_id(SystemModule::Json)))) return false;

  if (has_reserved_module(system_id(SystemModule::Buffer)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Buffer),
                                    reserved_aliases_for_id(system_id(SystemModule::Buffer)))) return false;
  if (has_reserved_module(system_id(SystemModule::Bytes)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Bytes),
                                    reserved_aliases_for_id(system_id(SystemModule::Bytes)))) return false;
  if (has_reserved_module(standard_id(StandardModule::Bytes)) &&
      !add_catalog_reserved_imports(standard_id(StandardModule::Bytes),
                                    reserved_aliases_for_id(standard_id(StandardModule::Bytes)))) return false;

  if (has_reserved_module(system_id(SystemModule::Log)) &&
      !add_catalog_reserved_imports(system_id(SystemModule::Log),
                                    reserved_aliases_for_id(system_id(SystemModule::Log)))) return false;
  if (has_reserved_module(standard_id(StandardModule::Log)) &&
      !add_catalog_reserved_imports(standard_id(StandardModule::Log),
                                    reserved_aliases_for_id(standard_id(StandardModule::Log)))) return false;

  for (const LibraryModuleId native_reserved : {system_id(SystemModule::IO),
                                               system_id(SystemModule::FFI),
                                               system_id(SystemModule::Thread),
                                               system_id(SystemModule::Path),
                                               system_id(SystemModule::FS)}) {
    if (has_reserved_module(native_reserved) &&
        !add_native_reserved_imports(std::string(ToCanonicalName(native_reserved)),
                                     reserved_aliases_for_id(native_reserved))) {
      return false;
    }
  }

  for (const auto* artifact : artifacts) {
    EmitState::ArtifactLayout layout;
    uint32_t offset = 0;
    uint32_t max_align = 1;
    layout.fields.reserve(artifact->fields.size());
    std::vector<const VarDecl*> ordered_fields;
    ordered_fields.reserve(artifact->fields.size());
    for (const auto& field : artifact->fields) ordered_fields.push_back(&field);
    if (!artifact->is_data) {
      std::stable_sort(ordered_fields.begin(), ordered_fields.end(), [](const VarDecl* a, const VarDecl* b) {
        uint32_t align_a = FieldAlignForType(a->type);
        uint32_t align_b = FieldAlignForType(b->type);
        if (align_a != align_b) return align_a > align_b;
        return FieldSizeForType(a->type) > FieldSizeForType(b->type);
      });
    }
    for (const VarDecl* field_ptr : ordered_fields) {
      const auto& field = *field_ptr;
      EmitState::FieldLayout field_layout;
      field_layout.name = field.name;
      if (!CloneTypeRef(field.type, &field_layout.type)) return false;
      field_layout.sir_type = FieldSirTypeName(field.type, st);
      uint32_t align = FieldAlignForType(field.type);
      uint32_t size = FieldSizeForType(field.type);
      offset = AlignTo(offset, align);
      field_layout.offset = offset;
      offset += size;
      if (align > max_align) max_align = align;
      layout.field_index[field.name] = layout.fields.size();
      layout.fields.push_back(std::move(field_layout));
    }
    layout.size = AlignTo(offset, max_align);
    st.artifact_layouts.emplace(artifact->name, std::move(layout));
  }

  for (const auto& entry : st.abi_types) {
    const auto& abi = entry.second;
    EmitState::ArtifactLayout layout;
    uint32_t offset = 0;
    uint32_t max_align = 1;
    layout.fields.reserve(abi.fields.size());
    for (const auto& field : abi.fields) {
      EmitState::FieldLayout field_layout;
      field_layout.name = field.abi_name;
      if (!CloneTypeRef(field.type, &field_layout.type)) return false;
      field_layout.sir_type = FieldSirTypeName(field.type, st);
      uint32_t align = FieldAlignForType(field.type);
      uint32_t size = FieldSizeForType(field.type);
      offset = AlignTo(offset, align);
      field_layout.offset = offset;
      offset += size;
      if (align > max_align) max_align = align;
      layout.field_index[field_layout.name] = layout.fields.size();
      layout.fields.push_back(std::move(field_layout));
    }
    layout.size = AlignTo(offset, max_align);
    st.artifact_layouts.emplace(abi.name, std::move(layout));
  }

  std::string entry_name;
  if (has_main) {
    entry_name = main_emit_name;
  } else if (has_top_level_script) {
    entry_name = script_entry_fn.name;
  } else {
    entry_name = functions[0].emit_name;
    for (const auto& fn : functions) {
      if (fn.decl->name == "main") {
        entry_name = fn.emit_name;
        break;
      }
    }
  }

  std::vector<std::string> function_text;
  function_text.reserve(functions.size());
  for (const auto& item : functions) {
    std::string func_body;
    if (!EmitFunction(st,
                      *item.decl,
                      item.emit_name,
                      item.display_name,
                      item.has_self ? &item.self_type : nullptr,
                      item.emit_name == entry_name,
                      item.script_body,
                      &func_body,
                      error)) {
      return false;
    }
    function_text.push_back(std::move(func_body));
  }

  for (size_t i = 0; i < st.lambda_funcs.size(); ++i) {
    const FuncDecl lambda = st.lambda_funcs[i];
    std::string func_body;
    if (!EmitFunction(st,
                      lambda,
                      lambda.name,
                      lambda.name,
                      nullptr,
                      false,
                      nullptr,
                      &func_body,
                      error)) {
      return false;
    }
    function_text.push_back(std::move(func_body));
  }

  std::ostringstream result;
  result << "sir version " << kSirVersionMajor << "." << kSirVersionMinor << "\n";
  if (!artifacts.empty() || !enums.empty()) {
    result << "types:\n";
    for (const auto* artifact : artifacts) {
      auto it = st.artifact_layouts.find(artifact->name);
      if (it == st.artifact_layouts.end()) return false;
      const auto& layout = it->second;
      const char* kind = artifact->tagged_kind == TaggedArtifactKind::Optional
                             ? "optional"
                             : (artifact->tagged_kind == TaggedArtifactKind::Result
                                    ? "result"
                                    : (artifact->is_data ? "data" : "artifact"));
      result << "  type " << artifact->name << " size=" << layout.size
             << " kind=" << kind << "\n";
      for (const auto& field : layout.fields) {
        result << "  field " << field.name << " " << field.sir_type << " offset=" << field.offset << "\n";
      }
    }
    for (const auto& entry : st.abi_types) {
      const auto& abi = entry.second;
      auto it = st.artifact_layouts.find(abi.name);
      if (it == st.artifact_layouts.end()) return false;
      const auto& layout = it->second;
      result << "  type " << abi.name << " size=" << layout.size << " kind=artifact\n";
      for (const auto& field : layout.fields) {
        result << "  field " << field.name << " " << field.sir_type << " offset=" << field.offset << "\n";
      }
    }
    for (const auto* enm : enums) {
      result << "  type " << enm->name << " size=4 kind=i32\n";
    }
  }

  result << "sigs:\n";
  struct SigItem {
    const FuncDecl* decl = nullptr;
    std::string name;
    bool has_self = false;
    TypeRef self_type;
  };
  std::vector<SigItem> all_functions;
  all_functions.reserve(functions.size() + st.lambda_funcs.size());
  for (const auto& item : functions) {
    SigItem sig;
    sig.decl = item.decl;
    sig.name = item.emit_name;
    sig.has_self = item.has_self;
    if (item.has_self) {
      if (!CloneTypeRef(item.self_type, &sig.self_type)) return false;
    }
    all_functions.push_back(std::move(sig));
  }
  for (const auto& fn : st.lambda_funcs) {
    all_functions.push_back({&fn, fn.name, false, {}});
  }
  for (const auto& fn : all_functions) {
    std::string ret = SigTypeNameFromType(fn.decl->return_type, st, error);
    if (ret.empty()) {
      if (error && error->empty()) *error = "unsupported return type in signature: " + fn.decl->return_type.name;
      return false;
    }
    result << "  sig " << fn.name << ": (";
    bool first = true;
    if (fn.has_self) {
      std::string param = SigTypeNameFromType(fn.self_type, st, error);
      if (param.empty()) {
        if (error && error->empty()) *error = "unsupported self type in signature";
        return false;
      }
      result << param;
      first = false;
    }
    for (size_t i = 0; i < fn.decl->params.size(); ++i) {
      if (!first) result << ", ";
      std::string param = SigTypeNameFromType(fn.decl->params[i].type, st, error);
      if (param.empty()) {
        if (error && error->empty()) {
          *error = "unsupported param type in signature: " + fn.decl->params[i].type.name;
        }
        return false;
      }
      result << param;
      first = false;
    }
    result << ") -> " << ret << "\n";
  }
  for (const auto& imp : st.imports) {
    std::string ret = SigTypeNameFromType(imp.ret, st, error);
    if (ret.empty()) {
      if (error && error->empty()) *error = "unsupported return type in import signature";
      return false;
    }
    result << "  sig " << imp.sig_name << ": (";
    bool first = true;
    for (size_t i = 0; i < imp.params.size(); ++i) {
      if (!first) result << ", ";
      std::string param = SigTypeNameFromType(imp.params[i], st, error);
      if (param.empty()) {
        if (error && error->empty()) *error = "unsupported param type in import signature";
        return false;
      }
      result << param;
      first = false;
    }
    result << ") -> " << ret << "\n";
  }
  for (const auto& line : st.proc_sig_lines) {
    result << line << "\n";
  }

  if (!globals.empty()) {
    for (const auto* glob : globals) {
      std::string init_const_name;
      if (!AddGlobalInitConst(st, glob->name, glob->type, &init_const_name)) {
        if (error) *error = "global '" + glob->name + "' type has no default const init support";
        return false;
      }
    }
  }

  if (!st.const_lines.empty()) {
    result << "consts:\n";
    for (const auto& line : st.const_lines) {
      result << line << "\n";
    }
  }

  if (!globals.empty()) {
    result << "globals:\n";
    for (const auto* glob : globals) {
      std::string type_name = SigTypeNameFromType(glob->type, st, error);
      if (type_name.empty()) {
        if (error && error->empty()) *error = "unsupported global type: " + glob->type.name;
        return false;
      }
      result << "  global " << glob->name << " " << type_name;
      result << " init=" << "__ginit_" + glob->name;
      result << "\n";
    }
  }

  if (!st.imports.empty()) {
    result << "imports:\n";
    for (const auto& imp : st.imports) {
      result << "  import " << imp.name << " " << imp.module << " " << imp.symbol
             << " sig=" << imp.sig_name;
      if (imp.flags != 0) {
        result << " flags=" << imp.flags;
      }
      result << "\n";
    }
  }

  for (const auto& text : function_text) {
    result << text;
  }

  result << "entry " << entry_name << "\n";

  if (out) *out = result.str();
  return true;
}

} // namespace

namespace IRE {

bool EmitSir(const Program& program, std::string* out, std::string* error) {
  std::string validate_error;
  if (!ValidateProgram(program, &validate_error)) {
    if (error) *error = validate_error;
    return false;
  }

  Program concrete_program;
  bool materialized = false;
  if (!GEN::MaterializeProgramForEmission(program, &concrete_program, &materialized, error)) {
    return false;
  }
  if (!materialized) return EmitProgramImpl(program, out, error);
  if (!ValidateProgram(concrete_program, &validate_error)) {
    if (error) *error = validate_error;
    return false;
  }
  return EmitProgramImpl(concrete_program, out, error);
}

bool EmitSirFromString(const std::string& text, std::string* out, std::string* error) {
  Program program;
  std::string parse_error;
  if (!CAST::ParseProgramFromString(text, &program, &parse_error)) {
    if (error) *error = parse_error;
    return false;
  }
  return EmitSir(program, out, error);
}

} // namespace IRE
} // namespace Simple::Lang
