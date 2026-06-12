#pragma once

// Phase-0 parser facade. New parser-facing code should include this header;
// legacy code may continue to include lang_parser.h during migration.

#include "CAST/cast.h"
#include "lang_parser.h"

namespace Simple::Lang::CAST {

using Parser = Simple::Lang::Parser;
using Simple::Lang::ParseProgramFromString;
using Simple::Lang::ParseTypeFromString;

} // namespace Simple::Lang::CAST
