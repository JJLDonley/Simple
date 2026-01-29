#ifndef SIMPLE_SBC_VERIFIER_H
#define SIMPLE_SBC_VERIFIER_H

#include <string>

#include "sbc_types.h"

namespace simplevm {

struct VerifyResult {
  bool ok = false;
  std::string error;
};

VerifyResult VerifyModule(const SbcModule& module);

} // namespace simplevm

#endif // SIMPLE_SBC_VERIFIER_H
