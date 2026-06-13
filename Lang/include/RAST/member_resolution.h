#pragma once

#include "RAST/rast.h"

namespace Simple::Lang::RAST {

MemberRefKind ClassifyMemberRefKind(MemberRefKind fallback, SymbolKind symbol_kind);
void AddResolvedMemberRef(ResolvedProgram* program,
                          MemberRefKind kind,
                          const std::string& base,
                          const std::string& member,
                          const std::string& qualified_name,
                          SymbolId symbol,
                          const std::string& receiver_type = {},
                          SymbolId receiver_symbol = kInvalidSymbolId);
void ResolveProgramMemberRefs(ResolvedProgram* program);

} // namespace Simple::Lang::RAST
