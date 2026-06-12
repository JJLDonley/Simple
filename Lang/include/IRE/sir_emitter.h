#pragma once

// Phase-0 IRE facade for SIR emission.
//
// Existing public APIs remain available through lang_sir.h. New code should
// include this header when it needs the language IR emitter boundary.

#include "IRB/ir_builder.h"
#include "lang_sir.h"

namespace Simple::Lang::IRE {

using Simple::Lang::EmitSir;
using Simple::Lang::EmitSirFromString;

} // namespace Simple::Lang::IRE
