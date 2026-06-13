#ifndef SIMPLE_VM_RUNTIME_LIMITS_H
#define SIMPLE_VM_RUNTIME_LIMITS_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "sbc_types.h"
#include "vm.h"

namespace Simple::VM::Runtime {

std::string CheckModuleLimits(const RuntimeLimits& limits, const Simple::Byte::SbcModule& module);
bool CheckSequenceLimit(const RuntimeLimits& limits, uint32_t count);
bool CheckCallDepthLimit(const RuntimeLimits& limits, size_t current_depth);

} // namespace Simple::VM::Runtime

#endif // SIMPLE_VM_RUNTIME_LIMITS_H
