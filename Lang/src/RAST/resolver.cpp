#include "RAST/resolver.h"

namespace Simple::Lang::RAST {
namespace {

bool AddSymbol(ResolvedProgram* out,
               SymbolKind kind,
               const std::string& name,
               const std::string& qualified_name,
               SymbolId parent,
               std::string* error) {
  if (!out) return false;
  if (out->by_qualified_name.find(qualified_name) != out->by_qualified_name.end()) {
    if (error) *error = "duplicate symbol: " + qualified_name;
    return false;
  }
  Symbol symbol;
  symbol.id = static_cast<SymbolId>(out->symbols.size());
  symbol.kind = kind;
  symbol.name = name;
  symbol.qualified_name = qualified_name;
  symbol.parent = parent;
  out->by_qualified_name.emplace(qualified_name, symbol.id);
  out->symbols.push_back(std::move(symbol));
  return true;
}

} // namespace

bool ResolveAstProgram(const Simple::Lang::AST::Program& program,
                       ResolvedProgram* out,
                       std::string* error) {
  if (!out) {
    if (error) *error = "missing RAST output program";
    return false;
  }
  out->program = &program;
  out->symbols.clear();
  out->by_qualified_name.clear();

  for (const auto& decl : program.decls) {
    switch (decl.kind) {
      case DeclKind::Import: {
        const std::string name = decl.import_decl.has_alias ? decl.import_decl.alias
                                                            : decl.import_decl.path;
        if (!AddSymbol(out, SymbolKind::Import, name, "import:" + name, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      }
      case DeclKind::Extern: {
        const std::string qualified = decl.ext.has_module ? decl.ext.module + "." + decl.ext.name
                                                          : decl.ext.name;
        if (!AddSymbol(out, SymbolKind::Extern, decl.ext.name, qualified, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      }
      case DeclKind::Function:
        if (!AddSymbol(out, SymbolKind::Function, decl.func.name, decl.func.name, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      case DeclKind::Variable:
        if (!AddSymbol(out, SymbolKind::Global, decl.var.name, decl.var.name, kInvalidSymbolId, error)) {
          return false;
        }
        break;
      case DeclKind::Artifact: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Artifact, decl.artifact.name, decl.artifact.name, kInvalidSymbolId, error)) {
          return false;
        }
        for (const auto& field : decl.artifact.fields) {
          if (!AddSymbol(out,
                         SymbolKind::ArtifactField,
                         field.name,
                         decl.artifact.name + "." + field.name,
                         parent,
                         error)) {
            return false;
          }
        }
        for (const auto& method : decl.artifact.methods) {
          if (!AddSymbol(out,
                         SymbolKind::ArtifactMethod,
                         method.name,
                         decl.artifact.name + "." + method.name,
                         parent,
                         error)) {
            return false;
          }
        }
        break;
      }
      case DeclKind::Module: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Module, decl.module.name, decl.module.name, kInvalidSymbolId, error)) {
          return false;
        }
        for (const auto& var : decl.module.variables) {
          if (!AddSymbol(out,
                         SymbolKind::ModuleVariable,
                         var.name,
                         decl.module.name + "." + var.name,
                         parent,
                         error)) {
            return false;
          }
        }
        for (const auto& fn : decl.module.functions) {
          if (!AddSymbol(out,
                         SymbolKind::ModuleFunction,
                         fn.name,
                         decl.module.name + "." + fn.name,
                         parent,
                         error)) {
            return false;
          }
        }
        break;
      }
      case DeclKind::Enum: {
        const SymbolId parent = static_cast<SymbolId>(out->symbols.size());
        if (!AddSymbol(out, SymbolKind::Enum, decl.enm.name, decl.enm.name, kInvalidSymbolId, error)) {
          return false;
        }
        for (const auto& member : decl.enm.members) {
          if (!AddSymbol(out,
                         SymbolKind::EnumMember,
                         member.name,
                         decl.enm.name + "." + member.name,
                         parent,
                         error)) {
            return false;
          }
        }
        break;
      }
    }
  }
  return true;
}

} // namespace Simple::Lang::RAST
