#pragma once

#include <string>

#include "Diagnostics/diagnostic.h"
#include "RAST/rast.h"
#include "TAST/tast.h"

namespace Simple::Lang {

bool ValidateProgram(const Program& program, std::string* error);
bool ValidateProgramFromString(const std::string& text, std::string* error);
bool ValidateProgramDiagnostic(const Program& program,
                               Diagnostics::Diagnostic* diagnostic);
bool ValidateProgramFromStringDiagnostic(const std::string& text,
                                         Diagnostics::Diagnostic* diagnostic);

} // namespace Simple::Lang

namespace Simple::Lang::TAST {

bool CheckResolvedProgram(const Simple::Lang::RAST::ResolvedProgram& resolved,
                          TypedProgram* out,
                          std::string* error);

} // namespace Simple::Lang::TAST
