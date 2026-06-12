#include "AST/lower_cast.h"

namespace Simple::Lang::AST {

bool LowerCastProgram(const Simple::Lang::CAST::Program& in,
                      Program* out,
                      std::string* error) {
  if (!out) {
    if (error) *error = "missing AST output program";
    return false;
  }
  *out = in;
  return true;
}

} // namespace Simple::Lang::AST
