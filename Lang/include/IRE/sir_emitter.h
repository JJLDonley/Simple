#pragma once

#include <string>

#include "IRB/ir_builder.h"
#include "lang_sir.h"

namespace Simple::Lang::IRE {

using Simple::Lang::EmitSir;
using Simple::Lang::EmitSirFromString;

bool EmitSirModule(const Simple::Lang::IRB::Module& module,
                   std::string* out,
                   std::string* error);

} // namespace Simple::Lang::IRE
