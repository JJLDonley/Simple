#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST/ast.h"
#include "RAST/rast.h"
#include "lang_library.h"

namespace Simple::Lang::RAST {

bool NativeModuleNameForReserved(LibraryModuleId module, std::string* out);
bool NativeModuleNameForReserved(const std::string& canonical_module, std::string* out);
std::vector<std::string> ReservedModuleMembers(LibraryModuleId module);
std::vector<std::string> ReservedModuleMembers(const std::string& canonical_module);
bool IsIoPrintName(const std::string& name);
bool IsReservedModuleFunction(const std::string& canonical_module, const std::string& member);
bool GetReservedModuleVarType(const std::string& canonical_module,
                              const std::string& member,
                              Simple::Lang::AST::TypeRef* out);
bool IsReservedModuleEnabled(const Simple::Lang::LibraryModuleSet& reserved_imports,
                             const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
                             const std::string& name);
bool ResolveReservedModuleId(const Simple::Lang::LibraryModuleSet& reserved_imports,
                             const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
                             const std::string& name,
                             LibraryModuleId* out);
bool ResolveReservedModuleName(const Simple::Lang::LibraryModuleSet& reserved_imports,
                               const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
                               const std::string& name,
                               std::string* out);
bool IsIoPrintCallExpr(const Simple::Lang::AST::Expr& callee,
                       const Simple::Lang::LibraryModuleSet& reserved_imports,
                       const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases);
bool IsCoreDlOpenCallExpr(const Simple::Lang::AST::Expr& expr,
                          const Simple::Lang::LibraryModuleSet& reserved_imports,
                          const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases);
bool GetDlOpenManifestModule(
    const Simple::Lang::AST::Expr& expr,
    const Simple::Lang::LibraryModuleSet& reserved_imports,
    const Simple::Lang::LibraryModuleAliasMap& reserved_import_aliases,
    const std::unordered_map<std::string, std::unordered_map<std::string, const Simple::Lang::AST::ExternDecl*>>& externs_by_module,
    std::string* out_module);
std::string NormalizeDlMemberName(const std::string& name);
bool GetModuleNameFromExpr(const Simple::Lang::AST::Expr& base, std::string* out);
bool GetDlOpenManifestModule(const ResolvedProgram* program,
                             const Simple::Lang::AST::Expr& expr,
                             std::string* out_module);

} // namespace Simple::Lang::RAST
