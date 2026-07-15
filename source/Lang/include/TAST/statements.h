#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool IsAssignOp(const std::string& op);
bool CheckProgramHasDeclarationsOrTopLevelStatements(const Simple::Lang::AST::Program& program,
                                                     std::string* error);
bool CheckEnumMemberValue(const Simple::Lang::AST::EnumMember& member, std::string* error);
bool IsCanonicalEnumUnderlyingType(const TypeRef& type);
bool ParseCanonicalEnumValue(const std::string& text,
                             const TypeRef& underlying_type,
                             uint64_t* value,
                             std::string* error);
std::string FormatCanonicalEnumValue(uint64_t value,
                                     const TypeRef& underlying_type);
bool CheckUniqueNamedMember(const std::string& name,
                            std::unordered_set<std::string>* seen,
                            const std::string& error_prefix,
                            std::string* error);
bool CheckAssignment(const Stmt& stmt, std::string* error);

} // namespace Simple::Lang::TAST
