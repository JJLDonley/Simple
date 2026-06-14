#pragma once

#include <string>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

bool CloneTypeRef(const Simple::Lang::AST::TypeRef& src,
                  Simple::Lang::AST::TypeRef* out);
bool IsPlainTypeRef(const Simple::Lang::AST::TypeRef& type);
bool IsPrimitiveTypeName(const std::string& name);
bool IsPrimitiveCastName(const std::string& name);
bool GetAtCastTargetName(const std::string& name, std::string* out_target);
bool IsIntegerScalarTypeName(const std::string& name);
bool IsIntegerTypeName(const std::string& name);
bool IsFloatScalarTypeName(const std::string& name);
bool IsFloatTypeName(const std::string& name);
bool IsBoolTypeName(const std::string& name);
bool IsStringTypeName(const std::string& name);
bool IsNumericTypeName(const std::string& name);
bool IsScalarType(const Simple::Lang::AST::TypeRef& type);

} // namespace Simple::Lang::TAST
