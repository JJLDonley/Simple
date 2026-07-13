#ifndef SIMPLE_VM_INTERPRETER_TRAPS_H
#define SIMPLE_VM_INTERPRETER_TRAPS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "interpreter/frames.h"
#include "sbc_types.h"
#include "vm.h"

namespace Simple::VM::Interpreter {

struct TrapContext {
  FrameState* current = nullptr;
  const std::vector<FrameState>* call_stack = nullptr;
  const Simple::Byte::SbcModule* module = nullptr;
  size_t pc = 0;
  size_t func_start = 0;
  uint8_t last_opcode = 0xFF;
};

struct TrapContextGuard {
  TrapContext* prev = nullptr;
  explicit TrapContextGuard(TrapContext* ctx);
  ~TrapContextGuard();
};

std::string LookupMethodName(const Simple::Byte::SbcModule& module, size_t func_index);
bool ReadU32Operand(const std::vector<uint8_t>& code, size_t offset, uint32_t& out_val);
bool ReadI32Operand(const std::vector<uint8_t>& code, size_t offset, int32_t& out_val);
ExecResult Trap(const std::string& message);

} // namespace Simple::VM::Interpreter

#endif // SIMPLE_VM_INTERPRETER_TRAPS_H
