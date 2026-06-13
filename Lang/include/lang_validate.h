#pragma once

// Legacy compatibility facade. New semantic-analysis code should include
// "RAST/resolver.h" and "TAST/type_checker.h".

#include <string>

#include "Diagnostics/diagnostic.h"
#include "lang_ast.h"

namespace Simple::Lang {

bool ValidateProgram(const Program& program, std::string* error);
bool ValidateProgramFromString(const std::string& text, std::string* error);
bool ValidateProgramDiagnostic(const Program& program,
                               Diagnostics::Diagnostic* diagnostic);
bool ValidateProgramFromStringDiagnostic(const std::string& text,
                                         Diagnostics::Diagnostic* diagnostic);

} // namespace Simple::Lang
