#pragma once

#include <string>

#include "lang_ast.h"

namespace Simple::Lang {

bool EmitSir(const Program& program, std::string* out, std::string* error);
bool EmitSirFromString(const std::string& text, std::string* out, std::string* error);

} // namespace Simple::Lang
