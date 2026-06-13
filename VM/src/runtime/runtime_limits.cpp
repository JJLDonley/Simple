#include "runtime/runtime_limits.h"

namespace Simple::VM::Runtime {

std::string CheckModuleLimits(const RuntimeLimits& limits, const Simple::Byte::SbcModule& module) {
  if (limits.max_const_pool_size != 0 && module.const_pool.size() > limits.max_const_pool_size) {
    return "runtime limit exceeded: const pool size";
  }
  if (limits.max_code_size != 0 && module.code.size() > limits.max_code_size) {
    return "runtime limit exceeded: code size";
  }
  for (const auto& func : module.functions) {
    if (limits.max_stack_slots != 0 && func.stack_max > limits.max_stack_slots) {
      return "runtime limit exceeded: max stack";
    }
  }
  for (const auto& method : module.methods) {
    if (limits.max_locals != 0 && method.local_count > limits.max_locals) {
      return "runtime limit exceeded: max locals";
    }
  }
  return {};
}

bool CheckSequenceLimit(const RuntimeLimits& limits, uint32_t count) {
  return limits.max_array_list_size == 0 || count <= limits.max_array_list_size;
}

bool CheckCallDepthLimit(const RuntimeLimits& limits, size_t current_depth) {
  return limits.max_call_depth == 0 || current_depth + 1 < limits.max_call_depth;
}

} // namespace Simple::VM::Runtime
