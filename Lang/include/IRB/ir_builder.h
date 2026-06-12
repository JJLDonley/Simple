#pragma once

#include <string>

#include "TAST/tast.h"

namespace Simple::Lang::IRB {

// Language IR module boundary. During migration this carries serialized SIR;
// later phases will replace it with structured functions/blocks/instructions.
struct Module {
  std::string sir_text;
};

bool BuildModule(const Simple::Lang::TAST::TypedProgram& typed,
                 Module* out,
                 std::string* error);

} // namespace Simple::Lang::IRB
