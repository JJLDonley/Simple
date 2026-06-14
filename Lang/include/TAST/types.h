#pragma once

#include <string>
#include <vector>

#include "AST/ast.h"

namespace Simple::Lang::TAST {

Simple::Lang::AST::TypeRef MakeSimpleType(const std::string& name);
Simple::Lang::AST::TypeRef MakeListType(const std::string& name);
bool CloneTypeRef(const Simple::Lang::AST::TypeRef& src,
                  Simple::Lang::AST::TypeRef* out);
bool CloneTypeVector(const std::vector<Simple::Lang::AST::TypeRef>& src,
                     std::vector<Simple::Lang::AST::TypeRef>* out);
bool CloneElementType(const Simple::Lang::AST::TypeRef& container,
                      Simple::Lang::AST::TypeRef* out);
bool TypeDimsEqual(const std::vector<Simple::Lang::AST::TypeDim>& a,
                   const std::vector<Simple::Lang::AST::TypeDim>& b);
bool TypeArgsEqual(const std::vector<Simple::Lang::AST::TypeRef>& a,
                   const std::vector<Simple::Lang::AST::TypeRef>& b);
bool TypeEquals(const Simple::Lang::AST::TypeRef& a,
                const Simple::Lang::AST::TypeRef& b);
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
bool IsListMethodName(const std::string& name);
bool IsScalarType(const Simple::Lang::AST::TypeRef& type);
bool IsLenCompatibleType(const Simple::Lang::AST::TypeRef& type);
bool CheckPrimitiveCastArgType(const std::string& cast_target,
                               const Simple::Lang::AST::TypeRef& arg_type,
                               std::string* error);

} // namespace Simple::Lang::TAST
