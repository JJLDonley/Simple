#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST/ast.h"
#include "RAST/rast.h"

namespace Simple::Lang::RAST {

bool NativeModuleNameForReserved(const std::string& canonical_module, std::string* out);
std::vector<std::string> ReservedModuleMembers(const std::string& canonical_module);
bool IsIoPrintName(const std::string& name);
bool IsReservedModuleFunction(const std::string& canonical_module, const std::string& member);
bool GetReservedModuleVarType(const std::string& canonical_module,
                              const std::string& member,
                              Simple::Lang::AST::TypeRef* out);
bool IsReservedModuleEnabled(const std::unordered_set<std::string>& reserved_imports,
                             const std::unordered_map<std::string, std::string>& reserved_import_aliases,
                             const std::string& name);
bool ResolveReservedModuleName(const std::unordered_set<std::string>& reserved_imports,
                               const std::unordered_map<std::string, std::string>& reserved_import_aliases,
                               const std::string& name,
                               std::string* out);
std::string NormalizeDlMemberName(const std::string& name);
bool GetModuleNameFromExpr(const Simple::Lang::AST::Expr& base, std::string* out);
bool GetDlOpenManifestModule(const ResolvedProgram* program,
                             const Simple::Lang::AST::Expr& expr,
                             std::string* out_module);

} // namespace Simple::Lang::RAST
