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
    const std::unordered_map<std::string, const Simple::Lang::AST::ArtifactDecl*>& artifacts,
    bool allow_void);

} // namespace Simple::Lang::TAST
