#pragma once

#include <string>

#include "TAST/tast.h"

namespace Simple::Lang::IRB {

// Phase-0 language IR module.
//
// Today lang_sir.cpp emits textual SIR directly. The staged split will move
// semantic lowering into IRB first, then make IRE responsible for serialization.
struct Module {
  std::string sir_text;
};

} // namespace Simple::Lang::IRB
