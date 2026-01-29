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

bool ReadU16(const std::vector<uint8_t>& code, size_t offset, uint16_t* out) {
  if (offset + 2 > code.size()) return false;
  *out = static_cast<uint16_t>(code[offset]) |
         (static_cast<uint16_t>(code[offset + 1]) << 8);
  return true;
}

bool ReadU32(const std::vector<uint8_t>& code, size_t offset, uint32_t* out) {
  if (offset + 4 > code.size()) return false;
  *out = static_cast<uint32_t>(code[offset]) |
         (static_cast<uint32_t>(code[offset + 1]) << 8) |
         (static_cast<uint32_t>(code[offset + 2]) << 16) |
         (static_cast<uint32_t>(code[offset + 3]) << 24);
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

    uint32_t method_id = func.method_id;
    if (method_id >= module.methods.size()) return Fail("function method id out of range");
    uint16_t local_count = module.methods[method_id].local_count;

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

      if (opcode == static_cast<uint8_t>(OpCode::Enter)) {
        uint16_t locals = 0;
        if (!ReadU16(code, pc + 1, &locals)) return Fail("ENTER operand out of bounds");
        if (locals != local_count) return Fail("ENTER local count mismatch");
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadLocal) ||
          opcode == static_cast<uint8_t>(OpCode::StoreLocal)) {
        uint32_t idx = 0;
        if (!ReadU32(code, pc + 1, &idx)) return Fail("local index out of bounds");
        if (idx >= local_count) return Fail("local index out of range");
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadGlobal) ||
          opcode == static_cast<uint8_t>(OpCode::StoreGlobal)) {
        uint32_t idx = 0;
        if (!ReadU32(code, pc + 1, &idx)) return Fail("global index out of bounds");
        if (idx >= module.globals.size()) return Fail("global index out of range");
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewObject)) {
        uint32_t type_id = 0;
        if (!ReadU32(code, pc + 1, &type_id)) return Fail("NEW_OBJECT type id out of bounds");
        if (type_id >= module.types.size()) return Fail("NEW_OBJECT bad type id");
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewArray) ||
          opcode == static_cast<uint8_t>(OpCode::NewList)) {
        uint32_t type_id = 0;
        if (!ReadU32(code, pc + 1, &type_id)) return Fail("NEW_ARRAY/LIST type id out of bounds");
        if (type_id >= module.types.size()) return Fail("NEW_ARRAY/LIST bad type id");
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadField) ||
          opcode == static_cast<uint8_t>(OpCode::StoreField)) {
        uint32_t field_id = 0;
        if (!ReadU32(code, pc + 1, &field_id)) return Fail("LOAD/STORE_FIELD id out of bounds");
        if (field_id >= module.fields.size()) return Fail("LOAD/STORE_FIELD bad field id");
      }
      if (opcode == static_cast<uint8_t>(OpCode::ConstString)) {
        uint32_t const_id = 0;
        if (!ReadU32(code, pc + 1, &const_id)) return Fail("CONST_STRING const id out of bounds");
        if (const_id + 8 > module.const_pool.size()) return Fail("CONST_STRING const id bad");
      }
      if (opcode == static_cast<uint8_t>(OpCode::Call) ||
          opcode == static_cast<uint8_t>(OpCode::TailCall)) {
        uint32_t func_id = 0;
        uint8_t arg_count = 0;
        if (!ReadU32(code, pc + 1, &func_id)) return Fail("CALL function id out of bounds");
        if (pc + 5 >= code.size()) return Fail("CALL arg count out of bounds");
        arg_count = code[pc + 5];
        if (func_id >= module.functions.size()) return Fail("CALL function id out of range");
        uint32_t callee_method = module.functions[func_id].method_id;
        if (callee_method >= module.methods.size()) return Fail("CALL method id out of range");
        uint32_t sig_id = module.methods[callee_method].sig_id;
        if (sig_id >= module.sigs.size()) return Fail("CALL signature id out of range");
        if (arg_count != module.sigs[sig_id].param_count) return Fail("CALL arg count mismatch");
      }
      if (opcode == static_cast<uint8_t>(OpCode::CallIndirect)) {
        uint32_t sig_id = 0;
        if (!ReadU32(code, pc + 1, &sig_id)) return Fail("CALL_INDIRECT sig id out of bounds");
        if (pc + 5 >= code.size()) return Fail("CALL_INDIRECT arg count out of bounds");
        uint8_t arg_count = code[pc + 5];
        if (sig_id >= module.sigs.size()) return Fail("CALL_INDIRECT signature id out of range");
        if (arg_count != module.sigs[sig_id].param_count) return Fail("CALL_INDIRECT arg count mismatch");
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
