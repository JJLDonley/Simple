#include "RAST/import_graph.h"

#include <utility>

#include "lang_reserved.h"

namespace Simple::Lang::RAST {

std::vector<ResolvedImport> ResolveReservedImports(const Simple::Lang::AST::Program* program) {
  std::vector<ResolvedImport> imports;
  if (!program) return imports;
  for (const auto& decl : program->decls) {
    if (decl.kind != Simple::Lang::AST::DeclKind::Import) continue;
    std::string resolved;
    if (!Simple::Lang::CanonicalizeReservedImportPath(decl.import_decl.path, &resolved)) continue;
    ResolvedImport import;
    import.alias = decl.import_decl.has_alias
        ? decl.import_decl.alias
        : Simple::Lang::DefaultImportAlias(resolved);
    import.canonical_module = resolved;
    imports.push_back(std::move(import));
  }
  return imports;
}

bool ResolveReservedImportAlias(const Simple::Lang::AST::Program* program,
                                const std::string& alias,
                                std::string* canonical) {
  for (const auto& import : ResolveReservedImports(program)) {
    if (import.alias == alias) {
      if (canonical) *canonical = import.canonical_module;
      return true;
    }
  }
  return false;
}

bool CheckUsingImportHasPriorAlias(const std::string& path,
                                   const std::unordered_map<std::string, std::string>& aliases,
                                   std::string* error) {
  if (aliases.find(path) == aliases.end()) {
    if (error) *error = "using requires prior import: " + path;
    return false;
  }
  return true;
}

} // namespace Simple::Lang::RAST
