#pragma once

#include <string>

#include "lang_ast.h"

namespace Simple::Lang {

bool ValidateProgram(const Program& program, std::string* error);
bool ValidateProgramFromString(const std::string& text, std::string* error);

} // namespace Simple::Lang
