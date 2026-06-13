#include "jit/jit_scaffold.h"

#include <cstdlib>
#include <limits>
#include <string>

#include "native/env.h"

namespace Simple::VM::Jit {

bool IsScalarKind(Simple::Byte::TypeKind kind) {
  switch (kind) {
    case Simple::Byte::TypeKind::I8:
    case Simple::Byte::TypeKind::I16:
    case Simple::Byte::TypeKind::I32:
    case Simple::Byte::TypeKind::I64:
    case Simple::Byte::TypeKind::U8:
    case Simple::Byte::TypeKind::U16:
    case Simple::Byte::TypeKind::U32:
    case Simple::Byte::TypeKind::U64:
    case Simple::Byte::TypeKind::F32:
    case Simple::Byte::TypeKind::F64:
    case Simple::Byte::TypeKind::Bool:
    case Simple::Byte::TypeKind::Char:
      return true;
    default:
      return false;
  }
}

bool IsValueKind(Simple::Byte::TypeKind kind) {
  return IsScalarKind(kind) || kind == Simple::Byte::TypeKind::Ref ||
         kind == Simple::Byte::TypeKind::String;
}

uint32_t ReadThreshold(const char* name, uint32_t fallback) {
  std::string owned_value;
  const char* raw = Simple::VM::Native::Env::Get(name, &owned_value);
  if (!raw || raw[0] == '\0') return fallback;
  char* end = nullptr;
  unsigned long parsed = std::strtoul(raw, &end, 10);
  if (end == raw || parsed == 0) return fallback;
  if (parsed > std::numeric_limits<uint32_t>::max()) {
    parsed = std::numeric_limits<uint32_t>::max();
  }
  return static_cast<uint32_t>(parsed);
}

Thresholds ReadThresholdsFromEnv() {
  Thresholds thresholds;
  thresholds.tier0 = ReadThreshold("SIMPLE_JIT_TIER0", kJitTier0Threshold);
  thresholds.tier1 = ReadThreshold("SIMPLE_JIT_TIER1", kJitTier1Threshold);
  thresholds.opcode = ReadThreshold("SIMPLE_JIT_OPCODE", kJitOpcodeThreshold);
  if (thresholds.tier0 == 0) thresholds.tier0 = kJitTier0Threshold;
  if (thresholds.tier1 < thresholds.tier0) thresholds.tier1 = thresholds.tier0;
  return thresholds;
}

} // namespace Simple::VM::Jit
