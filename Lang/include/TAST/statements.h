#pragma once

#include <string>

#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool IsAssignOp(const std::string& op);
bool CheckAssignment(const Stmt& stmt, std::string* error);

} // namespace Simple::Lang::TAST
