#include "jit/failure_format.h"

#include <sstream>

#include "interpreter/traps.h"
#include "opcode.h"

namespace Simple::VM::Jit {

std::string FormatCompiledFailure(const Simple::Byte::SbcModule& module,
                                  const Simple::Byte::FunctionRow& func,
                                  const char* message,
                                  uint8_t opcode,
                                  size_t instruction_pc) {
  std::ostringstream out;
  out << message << " op 0x";
  static const char kHex[] = "0123456789ABCDEF";
  out << kHex[(opcode >> 4) & 0xF] << kHex[opcode & 0xF];
  const char* name = Simple::Byte::OpCodeName(opcode);
  if (name && name[0] != '\0') {
    out << " " << name;
  }
  if (instruction_pc >= func.code_offset) {
    out << " pc " << (instruction_pc - func.code_offset);
  }
  if (instruction_pc + 1 < module.code.size()) {
    if (opcode == static_cast<uint8_t>(Simple::Byte::OpCode::Call)) {
      uint32_t func_id = 0;
      uint32_t arg_count = 0;
      if (Simple::VM::Interpreter::ReadU32Operand(module.code, instruction_pc + 1, func_id) &&
          (instruction_pc + 5) < module.code.size()) {
        arg_count = module.code[instruction_pc + 5];
        out << " operands call func_id=" << func_id << " arg_count=" << arg_count;
      }
    } else if (opcode == static_cast<uint8_t>(Simple::Byte::OpCode::Jmp) ||
               opcode == static_cast<uint8_t>(Simple::Byte::OpCode::JmpTrue) ||
               opcode == static_cast<uint8_t>(Simple::Byte::OpCode::JmpFalse)) {
      int32_t rel = 0;
      if (Simple::VM::Interpreter::ReadI32Operand(module.code, instruction_pc + 1, rel)) {
        int64_t next_pc = static_cast<int64_t>(instruction_pc + 1 + 4);
        int64_t target = next_pc + rel;
        out << " operands rel=" << rel;
        if (func.code_offset <= static_cast<size_t>(target)) {
          out << " target_pc=" << (target - static_cast<int64_t>(func.code_offset));
        } else {
          out << " target_pc=" << target;
        }
      }
    } else if (opcode == static_cast<uint8_t>(Simple::Byte::OpCode::JmpTable)) {
      uint32_t const_id = 0;
      int32_t def_rel = 0;
      if (Simple::VM::Interpreter::ReadU32Operand(module.code, instruction_pc + 1, const_id) &&
          Simple::VM::Interpreter::ReadI32Operand(module.code, instruction_pc + 5, def_rel)) {
        int64_t next_pc = static_cast<int64_t>(instruction_pc + 1 + 8);
        int64_t target = next_pc + def_rel;
        out << " operands table_const=" << const_id << " default_rel=" << def_rel;
        if (func.code_offset <= static_cast<size_t>(target)) {
          out << " default_target_pc=" << (target - static_cast<int64_t>(func.code_offset));
        } else {
          out << " default_target_pc=" << target;
        }
      }
    }
  }
  return out.str();
}

bool CompiledFailureReporter::operator()(const char* message, uint8_t opcode, size_t instruction_pc) const {
  if (!module || !function || !error) return false;
  *error = FormatCompiledFailure(*module, *function, message, opcode, instruction_pc);
  return false;
}

} // namespace Simple::VM::Jit
