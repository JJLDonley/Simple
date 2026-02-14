#include <string>

bool IsReservedModuleEnabled(const ValidateContext& ctx, const std::string& name) {
  if (ctx.reserved_import_aliases.find(name) != ctx.reserved_import_aliases.end()) return true;
  if (ctx.reserved_imports.find(name) != ctx.reserved_imports.end()) return true;
  std::string canonical;
  if (!CanonicalizeReservedImportPath(name, &canonical)) return false;
  return ctx.reserved_imports.find(canonical) != ctx.reserved_imports.end();
}

bool ResolveReservedModuleName(const ValidateContext& ctx,
                               const std::string& name,
                               std::string* out) {
  if (!out) return false;
  std::string canonical;
  if (CanonicalizeReservedImportPath(name, &canonical) &&
      ctx.reserved_imports.find(canonical) != ctx.reserved_imports.end()) {
    *out = canonical;
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
    if (root.kind == ExprKind::Identifier &&
        (root.text == "Core" || root.text == "System")) {
      *out = root.text + "." + base.text;
      return true;
    }
  }
  return false;
}
