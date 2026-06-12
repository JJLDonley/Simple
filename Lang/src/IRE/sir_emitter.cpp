#include "IRE/sir_emitter.h"

namespace Simple::Lang::IRE {

bool EmitSirModule(const Simple::Lang::IRB::Module& module,
                   std::string* out,
                   std::string* error) {
  if (!out) {
    if (error) *error = "missing SIR output string";
    return false;
  }
  if (!module.sir_lines.empty()) {
    out->clear();
    for (const auto& line : module.sir_lines) {
      *out += line;
      *out += '\n';
    }
    return true;
  }
  *out = module.sir_text;
  return true;
}

} // namespace Simple::Lang::IRE
