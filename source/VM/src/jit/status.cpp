#include "jit/status.h"

#include <cstring>

namespace Simple::VM::Jit {

const char* JitStatusCodeName(JitStatusCode code) {
  switch (code) {
    case JitStatusCode::Halt:
      return "halt";
    case JitStatusCode::Return:
      return "return";
    case JitStatusCode::Trap:
      return "trap";
    case JitStatusCode::Fallback:
      return "fallback";
    case JitStatusCode::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

JitStatusCode ClassifyJitReason(const char* reason) {
  if (!reason || reason[0] == '\0') return JitStatusCode::Return;
  if (std::strcmp(reason, "halt") == 0) return JitStatusCode::Halt;
  if (std::strncmp(reason, "trap", 4) == 0) return JitStatusCode::Trap;
  if (std::strcmp(reason, "unsupported") == 0 ||
      std::strncmp(reason, "unsupported:", 12) == 0) {
    return JitStatusCode::Unsupported;
  }
  return JitStatusCode::Fallback;
}

} // namespace Simple::VM::Jit
