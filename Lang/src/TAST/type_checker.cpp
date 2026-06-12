#include "TAST/type_checker.h"

#include "lang_validate.h"

namespace Simple::Lang::TAST {

bool CheckResolvedProgram(const Simple::Lang::RAST::ResolvedProgram& resolved,
                          TypedProgram* out,
                          std::string* error) {
  if (!resolved.program) {
    if (error) *error = "missing resolved program input";
    return false;
  }
  if (!ValidateProgram(*resolved.program, error)) return false;
  if (out) out->resolved = &resolved;
  return true;
}

} // namespace Simple::Lang::TAST
