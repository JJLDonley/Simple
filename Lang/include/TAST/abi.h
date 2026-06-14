#pragma once

#include <string>

#include "AST/ast.h"
#include "sbc_types.h"

namespace Simple::Lang::TAST {

bool CheckAbiShape(const Simple::Lang::AST::TypeRef& type,
                   bool allow_void,
                   std::string* error);
bool NativeTypeToLangType(Simple::Byte::TypeKind kind,
                          Simple::Lang::AST::TypeRef* out);

} // namespace Simple::Lang::TAST
