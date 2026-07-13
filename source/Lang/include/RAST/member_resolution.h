#pragma once

#include "RAST/rast.h"

#include <vector>

namespace Simple::Lang::RAST {

std::vector<std::string> ModuleMembers(const ModuleDecl* module);
std::string UnknownMemberErrorWithSuggestion(const std::string& module_name,
                                             const std::string& member,
                                             const std::vector<std::string>& candidates);
const VarDecl* FindModuleVar(const ModuleDecl* module, const std::string& name);
const FuncDecl* FindModuleFunc(const ModuleDecl* module, const std::string& name);
const VarDecl* FindArtifactField(const ArtifactDecl* artifact, const std::string& name);
const FuncDecl* FindArtifactMethod(const ArtifactDecl* artifact, const std::string& name);
bool IsArtifactMemberName(const ArtifactDecl* artifact, const std::string& name);

MemberRefKind ClassifyMemberRefKind(MemberRefKind fallback, SymbolKind symbol_kind);
void AddResolvedMemberRef(ResolvedProgram* program,
                          MemberRefKind kind,
                          const std::string& base,
                          const std::string& member,
                          const std::string& qualified_name,
                          SymbolId symbol,
                          const std::string& receiver_type = {},
                          SymbolId receiver_symbol = kInvalidSymbolId);
const MemberRef* LookupResolvedMemberRef(const ResolvedProgram* program,
                                         const std::string& base,
                                         const std::string& member);
const MemberRef* ResolveMemberAccess(const ResolvedProgram* program,
                                     const std::string& base,
                                     const std::string& member);
void ResolveProgramMemberRefs(ResolvedProgram* program);

} // namespace Simple::Lang::RAST
