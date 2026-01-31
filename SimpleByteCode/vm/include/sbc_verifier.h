#ifndef SIMPLE_SBC_VERIFIER_H
#define SIMPLE_SBC_VERIFIER_H

#include <cstdint>
#include <string>
#include <vector>

#include "sbc_types.h"

namespace simplevm {

enum class VmType : uint8_t {
  Unknown = 0,
  I32 = 1,
  I64 = 2,
  F32 = 3,
  F64 = 4,
  Ref = 5,
};

struct StackMap {
  uint32_t pc = 0;
  uint32_t stack_height = 0;
  std::vector<uint8_t> ref_bits;
};

struct MethodVerifyInfo {
  std::vector<VmType> locals;
  std::vector<uint8_t> locals_ref_bits;
  std::vector<StackMap> stack_maps;
};

struct VerifyResult {
  bool ok = false;
  std::string error;
  std::vector<MethodVerifyInfo> methods;
  std::vector<uint8_t> globals_ref_bits;
};

VerifyResult VerifyModule(const SbcModule& module);

} // namespace simplevm

#endif // SIMPLE_SBC_VERIFIER_H
