#include "IRE/sir_emitter.h"

namespace Simple::Lang::IRE {

bool EmitSirModule(const Simple::Lang::IRB::Module& module,
                   std::string* out,
                   std::string* error) {
  if (!out) {
    if (error) *error = "missing SIR output string";
    return false;
  }
  *out = module.sir_text;
  return true;
}

} // namespace Simple::Lang::IRE
