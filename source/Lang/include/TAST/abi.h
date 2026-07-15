#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "AST/ast.h"
#include "sbc_types.h"

namespace Simple::Lang::TAST {

bool CheckAbiShape(const Simple::Lang::AST::TypeRef& type,
                   bool allow_void,
                   std::string* error);
bool NativeTypeToLangType(Simple::Byte::TypeKind kind,
                          Simple::Lang::AST::TypeRef* out);
bool IsSupportedDlAbiType(
    const Simple::Lang::AST::TypeRef& type,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::AggregateDecl*>& aggregates,
    bool allow_void);
bool CheckExternAbiType(
    const Simple::Lang::AST::TypeRef& type,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::AggregateDecl*>& aggregates,
    bool allow_void,
    const std::string& error_message,
    std::string* error);
bool CheckDlDynamicSignature(
    const Simple::Lang::AST::ExternDecl& ext,
    const std::unordered_set<std::string>& enum_types,
    const std::unordered_map<std::string, const Simple::Lang::AST::AggregateDecl*>& aggregates,
    std::string* error);

} // namespace Simple::Lang::TAST
