#include "RAST/import_graph.h"

#include "lang_reserved.h"

namespace Simple::Lang::RAST {

bool ResolveReservedImportAlias(const Simple::Lang::AST::Program* program,
                                const std::string& alias,
                                std::string* canonical) {
  if (!program) return false;
  for (const auto& decl : program->decls) {
    if (decl.kind != Simple::Lang::AST::DeclKind::Import) continue;
    std::string resolved;
    if (!Simple::Lang::CanonicalizeReservedImportPath(decl.import_decl.path, &resolved)) continue;
    const std::string import_alias = decl.import_decl.has_alias
        ? decl.import_decl.alias
        : Simple::Lang::DefaultImportAlias(resolved);
    if (import_alias == alias) {
      if (canonical) *canonical = resolved;
      return true;
    }
  }
  return false;
}

} // namespace Simple::Lang::RAST
