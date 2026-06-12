#include "IRB/ir_builder.h"

#include "lang_sir.h"

namespace Simple::Lang::IRB {

bool BuildModule(const Simple::Lang::TAST::TypedProgram& typed,
                 Module* out,
                 std::string* error) {
  if (!out) {
    if (error) *error = "missing IRB output module";
    return false;
  }
  if (!typed.resolved || !typed.resolved->program) {
    if (error) *error = "missing typed program input";
    return false;
  }
  out->sir_text.clear();
  return Simple::Lang::EmitSir(*typed.resolved->program, &out->sir_text, error);
}

} // namespace Simple::Lang::IRB
