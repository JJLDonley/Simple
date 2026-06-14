#pragma once

#include <string>
#include <unordered_set>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool IsAssignOp(const std::string& op);
bool CheckEnumMemberValue(const Simple::Lang::AST::EnumMember& member, std::string* error);
bool CheckUniqueNamedMember(const std::string& name,
                            std::unordered_set<std::string>* seen,
                            const std::string& error_prefix,
                            std::string* error);
bool CheckAssignment(const Stmt& stmt, std::string* error);

} // namespace Simple::Lang::TAST
