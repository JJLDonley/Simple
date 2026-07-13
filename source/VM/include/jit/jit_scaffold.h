#ifndef SIMPLE_VM_JIT_SCAFFOLD_H
#define SIMPLE_VM_JIT_SCAFFOLD_H

#include <cstdint>

#include "sbc_types.h"
#include "vm.h"

namespace Simple::VM::Jit {

struct Stub {
  bool active = false;
  bool compiled = false;
  bool disabled = false;
};

struct Thresholds {
  uint32_t tier0 = kJitTier0Threshold;
  uint32_t tier1 = kJitTier1Threshold;
  uint32_t opcode = kJitOpcodeThreshold;
};

bool IsScalarKind(Simple::Byte::TypeKind kind);
bool IsValueKind(Simple::Byte::TypeKind kind);
uint32_t ReadThreshold(const char* name, uint32_t fallback);
Thresholds ReadThresholdsFromEnv();

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_SCAFFOLD_H
