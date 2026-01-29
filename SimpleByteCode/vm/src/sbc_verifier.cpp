#include "sbc_verifier.h"

#include <cstdint>
#include <unordered_map>
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

    enum class ValType { Unknown, I32, Bool, Ref };
    pc = func.code_offset;
    int stack_height = 0;
    std::unordered_map<size_t, std::vector<ValType>> merge_types;
    std::vector<ValType> stack_types;
    std::vector<ValType> locals(local_count, ValType::Unknown);
    std::vector<ValType> globals(module.globals.size(), ValType::Unknown);
    int call_depth = 0;
    auto pop_type = [&]() -> ValType {
      if (stack_types.empty()) return ValType::Unknown;
      ValType t = stack_types.back();
      stack_types.pop_back();
      return t;
    };
    auto push_type = [&](ValType t) { stack_types.push_back(t); };
    auto check_type = [&](ValType got, ValType expected, const char* msg) -> VerifyResult {
      if (got != ValType::Unknown && got != expected) return Fail(msg);
      return {true, ""};
    };
    while (pc < end) {
      uint8_t opcode = code[pc];
      OpInfo info{};
      GetOpInfo(opcode, &info);
      size_t next = pc + 1 + static_cast<size_t>(info.operand_bytes);

      bool has_jump_target = false;
      size_t jump_target = 0;
      bool fall_through = true;
      if (opcode == static_cast<uint8_t>(OpCode::Jmp) ||
          opcode == static_cast<uint8_t>(OpCode::JmpTrue) ||
          opcode == static_cast<uint8_t>(OpCode::JmpFalse)) {
        int32_t offset = 0;
        if (!ReadI32(code, pc + 1, &offset)) return Fail("jump operand out of bounds");
        jump_target = static_cast<size_t>(static_cast<int64_t>(next) + offset);
        if (jump_target < func.code_offset || jump_target > end) return Fail("jump target out of bounds");
        if (boundaries.find(jump_target) == boundaries.end()) return Fail("jump target not on instruction boundary");
        has_jump_target = true;
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
        if (opcode == static_cast<uint8_t>(OpCode::Call)) ++call_depth;
      }
      if (opcode == static_cast<uint8_t>(OpCode::CallIndirect)) {
        uint32_t sig_id = 0;
        if (!ReadU32(code, pc + 1, &sig_id)) return Fail("CALL_INDIRECT sig id out of bounds");
        if (pc + 5 >= code.size()) return Fail("CALL_INDIRECT arg count out of bounds");
        uint8_t arg_count = code[pc + 5];
        if (sig_id >= module.sigs.size()) return Fail("CALL_INDIRECT signature id out of range");
        if (arg_count != module.sigs[sig_id].param_count) return Fail("CALL_INDIRECT arg count mismatch");
      }

      switch (static_cast<OpCode>(opcode)) {
        case OpCode::Jmp:
          fall_through = false;
          break;
        case OpCode::ConstI8:
        case OpCode::ConstI16:
        case OpCode::ConstI32:
        case OpCode::ConstU8:
        case OpCode::ConstU16:
        case OpCode::ConstU32:
          push_type(ValType::I32);
          break;
        case OpCode::ConstBool:
          push_type(ValType::Bool);
          break;
        case OpCode::ConstNull:
        case OpCode::ConstString:
        case OpCode::NewObject:
        case OpCode::NewArray:
        case OpCode::NewList:
          push_type(ValType::Ref);
          break;
        case OpCode::LoadLocal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          if (idx < locals.size()) push_type(locals[idx]);
          else push_type(ValType::Unknown);
          break;
        }
        case OpCode::StoreLocal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          ValType t = pop_type();
          if (idx < locals.size()) {
            if (locals[idx] != ValType::Unknown && t != ValType::Unknown && locals[idx] != t) {
              return Fail("STORE_LOCAL type mismatch");
            }
            locals[idx] = t;
          }
          break;
        }
        case OpCode::LoadGlobal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          if (idx < globals.size()) push_type(globals[idx]);
          else push_type(ValType::Unknown);
          break;
        }
        case OpCode::StoreGlobal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          ValType t = pop_type();
          if (idx < globals.size()) {
            if (globals[idx] != ValType::Unknown && t != ValType::Unknown && globals[idx] != t) {
              return Fail("STORE_GLOBAL type mismatch");
            }
            globals[idx] = t;
          }
          break;
        }
        case OpCode::Pop:
          pop_type();
          break;
        case OpCode::Dup: {
          if (stack_types.empty()) return Fail("DUP underflow");
          push_type(stack_types.back());
          break;
        }
        case OpCode::Dup2: {
          if (stack_types.size() < 2) return Fail("DUP2 underflow");
          ValType a = stack_types[stack_types.size() - 2];
          ValType b = stack_types[stack_types.size() - 1];
          push_type(a);
          push_type(b);
          break;
        }
        case OpCode::Swap: {
          if (stack_types.size() < 2) return Fail("SWAP underflow");
          std::swap(stack_types[stack_types.size() - 1], stack_types[stack_types.size() - 2]);
          break;
        }
        case OpCode::Rot: {
          if (stack_types.size() < 3) return Fail("ROT underflow");
          ValType a = stack_types[stack_types.size() - 3];
          ValType b = stack_types[stack_types.size() - 2];
          ValType c = stack_types[stack_types.size() - 1];
          stack_types[stack_types.size() - 3] = b;
          stack_types[stack_types.size() - 2] = c;
          stack_types[stack_types.size() - 1] = a;
          break;
        }
        case OpCode::AddI32:
        case OpCode::SubI32:
        case OpCode::MulI32:
        case OpCode::DivI32:
        case OpCode::ModI32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I32, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I32, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I32, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I32, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::BoolNot: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Bool, "BOOL_NOT type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Bool, "BOOL op type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::Bool, "BOOL op type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Bool, "JMP type mismatch");
          if (!r.ok) return r;
          break;
        }
        case OpCode::IsNull: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "IS_NULL type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::RefEq:
        case OpCode::RefNe: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Ref, "REF type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::Ref, "REF type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::TypeOf: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "TYPEOF type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::LoadField: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "LOAD_FIELD type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::StoreField: {
          ValType v = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Ref, "STORE_FIELD type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(v, ValType::I32, "STORE_FIELD type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ArrayLen: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "ARRAY_LEN type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ArrayGetI32: {
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ArraySetI32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::I32, "ARRAY_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListLen: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "LIST_LEN type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListGetI32: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListSetI32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::I32, "LIST_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListPushI32: {
          ValType value = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(value, ValType::I32, "LIST_PUSH type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ListPopI32: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_POP type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::StringLen: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "STRING_LEN type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::StringConcat: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Ref, "STRING_CONCAT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::Ref, "STRING_CONCAT type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::CallCheck:
          if (call_depth != 0) return Fail("CALLCHECK not in root");
          break;
        case OpCode::Halt:
        case OpCode::Trap:
        case OpCode::TailCall:
        case OpCode::Ret:
          fall_through = false;
          break;
        default:
          for (int i = 0; i < info.pops; ++i) pop_type();
          for (int i = 0; i < info.pushes; ++i) push_type(ValType::Unknown);
          break;
      }

      if (info.pops > 0) {
        if (stack_height - info.pops < 0) return Fail("stack underflow");
        stack_height -= info.pops;
      }
      stack_height += info.pushes;
      if (has_jump_target) {
        auto it = merge_types.find(jump_target);
        if (it == merge_types.end()) {
          merge_types[jump_target] = stack_types;
        } else {
          if (it->second.size() != stack_types.size()) return Fail("stack merge height mismatch");
          for (size_t i = 0; i < it->second.size(); ++i) {
            if (it->second[i] == ValType::Unknown) it->second[i] = stack_types[i];
            else if (stack_types[i] != ValType::Unknown && it->second[i] != stack_types[i]) {
              return Fail("stack merge type mismatch");
            }
          }
        }
      }

      if (fall_through) {
        auto merge_it = merge_types.find(next);
        if (merge_it != merge_types.end()) {
          if (merge_it->second.size() != stack_types.size()) return Fail("stack merge height mismatch");
          for (size_t i = 0; i < stack_types.size(); ++i) {
            if (stack_types[i] == ValType::Unknown) stack_types[i] = merge_it->second[i];
            else if (merge_it->second[i] != ValType::Unknown && merge_it->second[i] != stack_types[i]) {
              return Fail("stack merge type mismatch");
            }
          }
        }
      } else {
        auto merge_it = merge_types.find(next);
        if (merge_it != merge_types.end()) {
          stack_types = merge_it->second;
        } else {
          stack_types.clear();
        }
        stack_height = static_cast<int>(stack_types.size());
      }
      pc = next;
    }
  }

  VerifyResult result;
  result.ok = true;
  return result;
}

} // namespace simplevm
