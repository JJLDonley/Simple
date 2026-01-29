#include "sbc_verifier.h"

#include <cstdint>
#include <unordered_set>

#include "opcode.h"

namespace simplevm {
namespace {

bool ReadI32(const std::vector<uint8_t>& code, size_t offset, int32_t* out) {
  if (offset + 4 > code.size()) return false;
  uint32_t v = static_cast<uint32_t>(code[offset]) |
               (static_cast<uint32_t>(code[offset + 1]) << 8) |
               (static_cast<uint32_t>(code[offset + 2]) << 16) |
               (static_cast<uint32_t>(code[offset + 3]) << 24);
  *out = static_cast<int32_t>(v);
  return true;
}

VerifyResult Fail(const std::string& message) {
  VerifyResult result;
  result.ok = false;
  result.error = message;
  return result;
}

} // namespace

VerifyResult VerifyModule(const SbcModule& module) {
  const auto& code = module.code;
  for (size_t func_index = 0; func_index < module.functions.size(); ++func_index) {
    const auto& func = module.functions[func_index];
    if (func.code_offset + func.code_size > code.size()) {
      return Fail("function code out of bounds");
    }

    size_t pc = func.code_offset;
    size_t end = func.code_offset + func.code_size;
    std::unordered_set<size_t> boundaries;

    while (pc < end) {
      boundaries.insert(pc);
      uint8_t opcode = code[pc];
      OpInfo info{};
      if (!GetOpInfo(opcode, &info)) {
        return Fail("unknown opcode in verifier");
      }
      size_t next = pc + 1 + static_cast<size_t>(info.operand_bytes);
      if (next > end) return Fail("opcode operands out of bounds");
      pc = next;
    }

    if (pc != end) return Fail("function code does not align to instruction boundary");

    pc = func.code_offset;
    int stack_height = 0;
    while (pc < end) {
      uint8_t opcode = code[pc];
      OpInfo info{};
      GetOpInfo(opcode, &info);
      size_t next = pc + 1 + static_cast<size_t>(info.operand_bytes);

      if (opcode == static_cast<uint8_t>(OpCode::Jmp) ||
          opcode == static_cast<uint8_t>(OpCode::JmpTrue) ||
          opcode == static_cast<uint8_t>(OpCode::JmpFalse)) {
        int32_t offset = 0;
        if (!ReadI32(code, pc + 1, &offset)) return Fail("jump operand out of bounds");
        size_t target = static_cast<size_t>(static_cast<int64_t>(next) + offset);
        if (target < func.code_offset || target > end) return Fail("jump target out of bounds");
        if (boundaries.find(target) == boundaries.end()) return Fail("jump target not on instruction boundary");
      }

      if (info.pops > 0) {
        if (stack_height - info.pops < 0) return Fail("stack underflow");
        stack_height -= info.pops;
      }
      stack_height += info.pushes;
      pc = next;
    }
  }

  VerifyResult result;
  result.ok = true;
  return result;
}

} // namespace simplevm
