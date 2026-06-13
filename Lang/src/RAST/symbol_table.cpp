#include "RAST/symbol_table.h"

#include <utility>

namespace Simple::Lang::RAST {

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

const Symbol* LookupSymbol(const ResolvedProgram* program, SymbolId symbol_id) {
  if (!program || symbol_id >= program->symbols.size()) return nullptr;
  return &program->symbols[symbol_id];
}

const Symbol* LookupQualifiedSymbol(const ResolvedProgram* program,
                                    const std::string& qualified_name) {
  if (!program || qualified_name.empty()) return nullptr;
  auto it = program->by_qualified_name.find(qualified_name);
  if (it == program->by_qualified_name.end()) return nullptr;
  return LookupSymbol(program, it->second);
}

SymbolId FindQualifiedSymbol(const ResolvedProgram* program,
                             const std::string& qualified_name,
                             SymbolKind expected_kind) {
  const Symbol* symbol = LookupQualifiedSymbol(program, qualified_name);
  if (!symbol || symbol->kind != expected_kind) return kInvalidSymbolId;
  return symbol->id;
}

} // namespace Simple::Lang::RAST
