#pragma once

#include <string>

#include "RAST/rast.h"
#include "TAST/tast.h"

namespace Simple::Lang::TAST {

bool CheckResolvedProgram(const Simple::Lang::RAST::ResolvedProgram& resolved,
                          TypedProgram* out,
                          std::string* error);

} // namespace Simple::Lang::TAST
