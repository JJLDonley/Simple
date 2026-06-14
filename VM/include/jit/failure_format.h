#ifndef SIMPLE_VM_JIT_FAILURE_FORMAT_H
#define SIMPLE_VM_JIT_FAILURE_FORMAT_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "sbc_types.h"

namespace Simple::VM::Jit {

std::string FormatCompiledFailure(const Simple::Byte::SbcModule& module,
                                  const Simple::Byte::FunctionRow& func,
                                  const char* message,
                                  uint8_t opcode,
                                  size_t instruction_pc);

struct CompiledFailureReporter {
  const Simple::Byte::SbcModule* module = nullptr;
  const Simple::Byte::FunctionRow* function = nullptr;
  std::string* error = nullptr;

  bool operator()(const char* message, uint8_t opcode, size_t instruction_pc) const;
};

} // namespace Simple::VM::Jit

#endif // SIMPLE_VM_JIT_FAILURE_FORMAT_H
