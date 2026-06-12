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

bool LowerCastProgramNormalized(const Simple::Lang::CAST::Program& in,
                                NormalizedProgram* out,
                                std::string* error) {
  if (!out) {
    if (error) *error = "missing normalized AST output program";
    return false;
  }
  out->decls = in.decls;
  out->script_body.statements = in.top_level_stmts;
  return true;
}

} // namespace Simple::Lang::AST
