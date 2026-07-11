#include "RAST/reserved_resolution.h"

#include <algorithm>

#include "RAST/import_graph.h"
#include "lang_reserved.h"
#include "native/registry.h"

namespace Simple::Lang::RAST {

namespace {

const Simple::VM::Native::NativeRegistry& ReservedNativeRegistry() {
  static const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  return registry;
}

} // namespace

bool NativeModuleNameForReserved(LibraryModuleId module, std::string* out) {
  if (!out) return false;
  const std::string_view native = ToNativeModule(module);
  if (native.empty()) return false;
  *out = std::string(native);
  return true;
}

bool NativeModuleNameForReserved(const std::string& canonical_module, std::string* out) {
  const auto module = ParseCanonicalLibraryModule(canonical_module);
  if (!module) return false;
  return NativeModuleNameForReserved(*module, out);
}

std::vector<std::string> ReservedModuleMembers(LibraryModuleId module) {
  std::vector<std::string> out;
  for (std::string_view member : MemberNames(module)) {
    if (IsImplementedLibraryMember(module, member)) out.emplace_back(member);
  }
  std::string native_module;
  if (NativeModuleNameForReserved(module, &native_module)) {
    for (const auto& spec : ReservedNativeRegistry().Functions()) {
      if (spec.module_name != native_module) continue;
      if (std::find(out.begin(), out.end(), spec.symbol_name) == out.end()) {
        out.push_back(spec.symbol_name);
      }
    }
  }
  return out;
}

std::vector<std::string> ReservedModuleMembers(const std::string& canonical_module) {
  const auto module = ParseCanonicalLibraryModule(canonical_module);
  if (!module) return {};
  return ReservedModuleMembers(*module);
}

bool IsIoPrintName(const std::string& name) {
  return name == "print" || name == "println";
}

bool GetReservedModuleVarType(const std::string& canonical_module,
                              const std::string& member,
                              Simple::Lang::AST::TypeRef* out) {
  auto set_simple = [out](const std::string& name) {
    if (out) {
      *out = Simple::Lang::AST::TypeRef{};
      out->name = name;
    }
    return true;
  };
  const auto module = ParseCanonicalLibraryModule(canonical_module);
  if (module && module->root == LibraryRoot::Standard &&
      static_cast<StandardModule>(module->module_index) == StandardModule::Math &&
      ParseMember(StandardModule::Math, member) == StandardMember(StandardMathMember::PI)) {
    return set_simple("f64");
  }
  if (module && module->root == LibraryRoot::System &&
      static_cast<SystemModule>(module->module_index) == SystemModule::FFI &&
      ParseMember(SystemModule::FFI, member) == SystemMember(SystemFFIMember::Supported)) {
    return set_simple("bool");
  }
  return false;
}

bool IsReservedModuleEnabled(const Simple::Lang::LibraryModuleSet& reserved_imports,
                             const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
                             const std::string& name) {
  if (reserved_import_aliases.find(name) != reserved_import_aliases.end()) return true;
  if (auto info = ParseLibraryImportPath(name)) {
    return reserved_imports.find(LibraryModuleId{info->root, info->module_index}) != reserved_imports.end();
  }
  if (auto module = ParseCanonicalLibraryModule(name)) {
    return reserved_imports.find(*module) != reserved_imports.end();
  }
  return false;
}

bool ResolveReservedModuleId(const Simple::Lang::LibraryModuleSet& reserved_imports,
                             const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
                             const std::string& name,
                             LibraryModuleId* out) {
  if (!out) return false;
  if (auto info = ParseLibraryImportPath(name)) {
    LibraryModuleId id{info->root, info->module_index};
    if (reserved_imports.find(id) != reserved_imports.end()) {
      *out = id;
      return true;
    }
  }
  if (auto module = ParseCanonicalLibraryModule(name)) {
    if (reserved_imports.find(*module) != reserved_imports.end()) {
      *out = *module;
      return true;
    }
  }
  auto it = reserved_import_aliases.find(name);
  if (it != reserved_import_aliases.end()) {
    *out = it->second;
    return true;
  }
  return false;
}

bool ResolveReservedModuleName(const Simple::Lang::LibraryModuleSet& reserved_imports,
                               const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
                               const std::string& name,
                               std::string* out) {
  LibraryModuleId id{};
  if (!out || !ResolveReservedModuleId(reserved_imports, reserved_import_aliases, name, &id)) return false;
  *out = std::string(ToCanonicalName(id));
  return true;
}

bool IsReservedModuleFunction(const std::string& canonical_module, const std::string& member) {
  const auto module = ParseCanonicalLibraryModule(canonical_module);
  if (module) {
    if (!IsImplementedLibraryMember(*module, member)) return false;
    if (module->root == LibraryRoot::System &&
        static_cast<SystemModule>(module->module_index) == SystemModule::FFI &&
        ParseMember(SystemModule::FFI, member) == SystemMember(SystemFFIMember::Supported)) {
      return false;
    }
    return true;
  }
  return false;
}

std::string NormalizeDlMemberName(const std::string& name) {
  return NormalizeSystemFFIMemberName(name);
}

bool GetModuleNameFromExpr(const Simple::Lang::AST::Expr& base, std::string* out) {
  if (!out) return false;
  if (base.kind == Simple::Lang::AST::ExprKind::Identifier) {
    *out = base.text;
    return true;
  }
  if (base.kind == Simple::Lang::AST::ExprKind::Member && base.op == "." && !base.children.empty()) {
    std::string prefix;
    if (!GetModuleNameFromExpr(base.children[0], &prefix)) return false;
    *out = prefix + "." + base.text;
    return true;
  }
  return false;
}

bool IsIoPrintCallExpr(const Simple::Lang::AST::Expr& callee,
                       const Simple::Lang::LibraryModuleSet& reserved_imports,
                       const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases) {
  if (callee.kind != Simple::Lang::AST::ExprKind::Member || callee.op != "." || callee.children.empty()) {
    return false;
  }
  if (!IsIoPrintName(callee.text)) return false;
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  LibraryModuleId resolved{};
  return ResolveReservedModuleId(reserved_imports, reserved_import_aliases, module_name, &resolved) &&
         resolved.root == LibraryRoot::Standard &&
         static_cast<StandardModule>(resolved.module_index) == StandardModule::IO;
}

bool IsCoreDlOpenCallExpr(const Simple::Lang::AST::Expr& expr,
                          const Simple::Lang::LibraryModuleSet& reserved_imports,
                          const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases) {
  if (expr.kind != Simple::Lang::AST::ExprKind::Call || expr.children.empty()) return false;
  const auto& callee = expr.children[0];
  if (callee.kind != Simple::Lang::AST::ExprKind::Member || callee.op != "." || callee.children.empty()) {
    return false;
  }
  std::string module_name;
  if (!GetModuleNameFromExpr(callee.children[0], &module_name)) return false;
  if (!IsReservedModuleEnabled(reserved_imports, reserved_import_aliases, module_name)) return false;
  LibraryModuleId resolved{};
  if (!ResolveReservedModuleId(reserved_imports, reserved_import_aliases, module_name, &resolved)) return false;
  return resolved.root == LibraryRoot::System &&
         static_cast<SystemModule>(resolved.module_index) == SystemModule::FFI &&
         ParseMember(SystemModule::FFI, NormalizeDlMemberName(callee.text)) == SystemMember(SystemFFIMember::Open);
}

bool GetDlOpenManifestModule(
    const Simple::Lang::AST::Expr& expr,
    const Simple::Lang::LibraryModuleSet& reserved_imports,
    const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
    const std::unordered_map<std::string, std::unordered_map<std::string, const Simple::Lang::AST::ExternDecl*>>& externs_by_module,
    std::string* out_module) {
  if (!out_module) return false;
  if (!IsCoreDlOpenCallExpr(expr, reserved_imports, reserved_import_aliases)) return false;
  if (expr.args.size() != 2) return false;
  if (expr.args[1].kind != Simple::Lang::AST::ExprKind::Identifier) return false;
  const std::string& module = expr.args[1].text;
  auto mod_it = externs_by_module.find(module);
  if (mod_it == externs_by_module.end() || mod_it->second.empty()) return false;
  *out_module = module;
  return true;
}

bool GetDlOpenManifestModule(const ResolvedProgram* program,
                             const Simple::Lang::AST::Expr& expr,
                             std::string* out_module) {
  if (!program || !out_module || expr.kind != Simple::Lang::AST::ExprKind::Call || expr.children.empty()) return false;
  const auto& callee = expr.children[0];
  if (callee.kind != Simple::Lang::AST::ExprKind::Member || callee.op != "." || callee.children.empty()) return false;
  if (NormalizeDlMemberName(callee.text) != "open") return false;
  std::string module_alias;
  if (!GetModuleNameFromExpr(callee.children[0], &module_alias)) return false;
  std::string canonical;
  if (!ResolveReservedImportAlias(program->program, module_alias, &canonical)) {
    for (const auto& import : ResolveReservedImports(program->program)) {
      if (import.canonical_module == module_alias) {
        canonical = import.canonical_module;
        break;
      }
    }
  }
  if (canonical.empty()) return false;
  const auto resolved = ParseCanonicalLibraryModule(canonical);
  if (!resolved || resolved->root != LibraryRoot::System ||
      static_cast<SystemModule>(resolved->module_index) != SystemModule::FFI) return false;
  if (expr.args.size() != 2 || expr.args[1].kind != Simple::Lang::AST::ExprKind::Identifier) return false;
  const std::string& manifest_module = expr.args[1].text;
  const std::string prefix = manifest_module + ".";
  for (const auto& entry : program->by_qualified_name) {
    if (entry.first.rfind(prefix, 0) == 0 && program->symbols[entry.second].kind == SymbolKind::Extern) {
      *out_module = manifest_module;
      return true;
    }
  }
  return false;
}

} // namespace Simple::Lang::RAST
