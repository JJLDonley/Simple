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
    uint32_t sig_id = module.methods[method_id].sig_id;
    if (sig_id >= module.sigs.size()) return Fail("function signature out of range");
    const auto& sig = module.sigs[sig_id];
    uint32_t ret_type_id = sig.ret_type_id;

    enum class ValType { Unknown, I32, I64, F32, F64, Bool, Ref };
    auto resolve_type = [&](uint32_t type_id) -> ValType {
      if (type_id >= module.types.size()) return ValType::Unknown;
      const auto& row = module.types[type_id];
      if ((row.flags & 0x1u) != 0u) return ValType::Ref;
      if (row.size == 0) return ValType::Ref;
      if (row.size == 1) return ValType::Bool;
      if (row.size == 4) return ValType::I32;
      if (row.size == 8) return ValType::I64;
      return ValType::Unknown;
    };
    bool expect_void = (ret_type_id == 0xFFFFFFFFu);
    ValType expected_ret = expect_void ? ValType::Unknown : resolve_type(ret_type_id);
    if (!expect_void && expected_ret == ValType::Unknown) return Fail("unsupported return type");

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

    boundaries.insert(end);
    if (pc != end) return Fail("function code does not align to instruction boundary");

    pc = func.code_offset;
    int stack_height = 0;
    std::unordered_map<size_t, std::vector<ValType>> merge_types;
    std::vector<ValType> stack_types;
    std::vector<ValType> locals(local_count, ValType::Unknown);
    std::vector<bool> locals_init(local_count, false);
    if (sig.param_count > local_count) return Fail("param count exceeds locals");
    if (sig.param_count > 0 &&
        sig.param_type_start + sig.param_count > module.param_types.size()) {
      return Fail("signature param types out of range");
    }
    for (uint16_t i = 0; i < sig.param_count && i < locals_init.size(); ++i) {
      uint32_t type_id = module.param_types[sig.param_type_start + i];
      ValType param_type = resolve_type(type_id);
      if (param_type == ValType::Unknown) return Fail("unsupported param type");
      locals[i] = param_type;
      locals_init[i] = true;
    }
    std::vector<ValType> globals(module.globals.size(), ValType::Unknown);
    std::vector<bool> globals_init(module.globals.size(), false);
    for (size_t i = 0; i < module.globals.size(); ++i) {
      if (module.globals[i].init_const_id != 0xFFFFFFFFu) globals_init[i] = true;
    }
    int call_depth = 0;
    auto pop_type = [&]() -> ValType {
      if (stack_types.empty()) return ValType::Unknown;
      ValType t = stack_types.back();
      stack_types.pop_back();
      return t;
    };
    auto push_type = [&](ValType t) { stack_types.push_back(t); };
    auto check_type = [&](ValType got, ValType expected, const char* msg) -> VerifyResult {
      if (expected == ValType::Unknown) return {true, ""};
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
      int extra_pops = 0;
      int extra_pushes = 0;
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
      if (opcode == static_cast<uint8_t>(OpCode::LoadUpvalue) ||
          opcode == static_cast<uint8_t>(OpCode::StoreUpvalue)) {
        uint32_t idx = 0;
        if (!ReadU32(code, pc + 1, &idx)) return Fail("upvalue index out of bounds");
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewObject)) {
        uint32_t type_id = 0;
        if (!ReadU32(code, pc + 1, &type_id)) return Fail("NEW_OBJECT type id out of bounds");
        if (type_id >= module.types.size()) return Fail("NEW_OBJECT bad type id");
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewClosure)) {
        uint32_t method_id = 0;
        if (!ReadU32(code, pc + 1, &method_id)) return Fail("NEW_CLOSURE method id out of bounds");
        if (pc + 5 >= code.size()) return Fail("NEW_CLOSURE upvalue count out of bounds");
        if (method_id >= module.methods.size()) return Fail("NEW_CLOSURE bad method id");
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
        case OpCode::ConstChar:
          push_type(ValType::I32);
          break;
        case OpCode::ConstI64:
        case OpCode::ConstU64:
          push_type(ValType::I64);
          break;
        case OpCode::ConstI128:
        case OpCode::ConstU128:
          push_type(ValType::Ref);
          break;
        case OpCode::ConstF32:
          push_type(ValType::F32);
          break;
        case OpCode::ConstF64:
          push_type(ValType::F64);
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
        case OpCode::NewClosure: {
          if (pc + 5 >= code.size()) return Fail("NEW_CLOSURE upvalue count out of bounds");
          uint8_t upvalue_count = code[pc + 5];
          for (int i = 0; i < static_cast<int>(upvalue_count); ++i) {
            ValType t = pop_type();
            VerifyResult r = check_type(t, ValType::Ref, "NEW_CLOSURE upvalue type mismatch");
            if (!r.ok) return r;
          }
          push_type(ValType::Ref);
          break;
        }
        case OpCode::LoadLocal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          if (idx < locals.size()) {
            if (!locals_init[idx]) return Fail("LOAD_LOCAL uninitialized");
            push_type(locals[idx]);
          } else {
            push_type(ValType::Unknown);
          }
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
            locals_init[idx] = true;
          }
          break;
        }
        case OpCode::LoadGlobal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          if (idx < globals.size()) {
            if (!globals_init[idx]) return Fail("LOAD_GLOBAL uninitialized");
            push_type(globals[idx]);
          }
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
            globals_init[idx] = true;
          }
          break;
        }
        case OpCode::LoadUpvalue:
          push_type(ValType::Ref);
          break;
        case OpCode::StoreUpvalue: {
          ValType t = pop_type();
          VerifyResult r = check_type(t, ValType::Ref, "STORE_UPVALUE type mismatch");
          if (!r.ok) return r;
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
        case OpCode::AddU32:
        case OpCode::SubU32:
        case OpCode::MulU32:
        case OpCode::DivU32:
        case OpCode::ModU32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I32, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I32, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::AndI32:
        case OpCode::OrI32:
        case OpCode::XorI32:
        case OpCode::ShlI32:
        case OpCode::ShrI32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I32, "bitwise type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I32, "bitwise type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::AddI64:
        case OpCode::SubI64:
        case OpCode::MulI64:
        case OpCode::DivI64:
        case OpCode::ModI64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::AddU64:
        case OpCode::SubU64:
        case OpCode::MulU64:
        case OpCode::DivU64:
        case OpCode::ModU64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::AndI64:
        case OpCode::OrI64:
        case OpCode::XorI64:
        case OpCode::ShlI64:
        case OpCode::ShrI64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "bitwise type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "bitwise type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::AddF32:
        case OpCode::SubF32:
        case OpCode::MulF32:
        case OpCode::DivF32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F32, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F32, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F32);
          break;
        }
        case OpCode::AddF64:
        case OpCode::SubF64:
        case OpCode::MulF64:
        case OpCode::DivF64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F64, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F64, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F64);
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
        case OpCode::CmpEqU32:
        case OpCode::CmpNeU32:
        case OpCode::CmpLtU32:
        case OpCode::CmpLeU32:
        case OpCode::CmpGtU32:
        case OpCode::CmpGeU32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I32, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I32, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqI64:
        case OpCode::CmpNeI64:
        case OpCode::CmpLtI64:
        case OpCode::CmpLeI64:
        case OpCode::CmpGtI64:
        case OpCode::CmpGeI64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqU64:
        case OpCode::CmpNeU64:
        case OpCode::CmpLtU64:
        case OpCode::CmpLeU64:
        case OpCode::CmpGtU64:
        case OpCode::CmpGeU64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqF32:
        case OpCode::CmpNeF32:
        case OpCode::CmpLtF32:
        case OpCode::CmpLeF32:
        case OpCode::CmpGtF32:
        case OpCode::CmpGeF32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F32, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F32, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqF64:
        case OpCode::CmpNeF64:
        case OpCode::CmpLtF64:
        case OpCode::CmpLeF64:
        case OpCode::CmpGtF64:
        case OpCode::CmpGeF64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F64, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F64, "compare type mismatch");
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
        case OpCode::ListInsertI32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_INSERT type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::I32, "LIST_INSERT type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListRemoveI32: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_REMOVE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_REMOVE type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListClear: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_CLEAR type mismatch");
          if (!r.ok) return r;
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
        case OpCode::StringGetChar: {
          ValType idx = pop_type();
          ValType str = pop_type();
          VerifyResult r1 = check_type(str, ValType::Ref, "STRING_GET_CHAR type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "STRING_GET_CHAR type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::StringSlice: {
          ValType end_idx = pop_type();
          ValType start_idx = pop_type();
          ValType str = pop_type();
          VerifyResult r1 = check_type(str, ValType::Ref, "STRING_SLICE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(start_idx, ValType::I32, "STRING_SLICE type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(end_idx, ValType::I32, "STRING_SLICE type mismatch");
          if (!r3.ok) return r3;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::CallCheck:
          if (call_depth != 0) return Fail("CALLCHECK not in root");
          break;
        case OpCode::Call: {
          if (pc + 5 >= code.size()) return Fail("CALL arg count out of bounds");
          uint8_t arg_count = code[pc + 5];
          if (stack_types.size() < arg_count) return Fail("CALL stack underflow");
          uint32_t func_id = 0;
          if (!ReadU32(code, pc + 1, &func_id)) return Fail("CALL function id out of bounds");
          if (func_id >= module.functions.size()) return Fail("CALL function id out of range");
          uint32_t callee_method = module.functions[func_id].method_id;
          if (callee_method >= module.methods.size()) return Fail("CALL method id out of range");
          uint32_t sig_id = module.methods[callee_method].sig_id;
          if (sig_id >= module.sigs.size()) return Fail("CALL signature id out of range");
          const auto& call_sig = module.sigs[sig_id];
          if (call_sig.param_count > 0 &&
              call_sig.param_type_start + call_sig.param_count > module.param_types.size()) {
            return Fail("CALL signature param types out of range");
          }
          for (int i = static_cast<int>(call_sig.param_count) - 1; i >= 0; --i) {
            ValType got = pop_type();
            ValType expected = ValType::Unknown;
            if (call_sig.param_count > 0) {
              uint32_t type_id = module.param_types[call_sig.param_type_start + static_cast<uint16_t>(i)];
              expected = resolve_type(type_id);
            }
            VerifyResult r = check_type(got, expected, "CALL arg type mismatch");
            if (!r.ok) return r;
          }
          if (call_sig.ret_type_id != 0xFFFFFFFFu) {
            ValType ret_type = resolve_type(call_sig.ret_type_id);
            push_type(ret_type);
            extra_pushes = 1;
          } else {
            extra_pushes = 0;
          }
          extra_pops = arg_count;
          break;
        }
        case OpCode::CallIndirect: {
          if (pc + 5 >= code.size()) return Fail("CALL_INDIRECT arg count out of bounds");
          uint8_t arg_count = code[pc + 5];
          if (stack_types.size() < static_cast<size_t>(arg_count) + 1u) return Fail("CALL_INDIRECT stack underflow");
          uint32_t sig_id = 0;
          if (!ReadU32(code, pc + 1, &sig_id)) return Fail("CALL_INDIRECT sig id out of bounds");
          if (sig_id >= module.sigs.size()) return Fail("CALL_INDIRECT signature id out of range");
          const auto& call_sig = module.sigs[sig_id];
          if (call_sig.param_count > 0 &&
              call_sig.param_type_start + call_sig.param_count > module.param_types.size()) {
            return Fail("CALL_INDIRECT signature param types out of range");
          }
          ValType func_type = pop_type();
          if (func_type != ValType::I32 && func_type != ValType::Ref && func_type != ValType::Unknown) {
            return Fail("CALL_INDIRECT func type mismatch");
          }
          for (int i = static_cast<int>(call_sig.param_count) - 1; i >= 0; --i) {
            ValType got = pop_type();
            ValType expected = ValType::Unknown;
            if (call_sig.param_count > 0) {
              uint32_t type_id = module.param_types[call_sig.param_type_start + static_cast<uint16_t>(i)];
              expected = resolve_type(type_id);
            }
            VerifyResult rarg = check_type(got, expected, "CALL_INDIRECT arg type mismatch");
            if (!rarg.ok) return rarg;
          }
          if (call_sig.ret_type_id != 0xFFFFFFFFu) {
            ValType ret_type = resolve_type(call_sig.ret_type_id);
            push_type(ret_type);
            extra_pushes = 1;
          } else {
            extra_pushes = 0;
          }
          extra_pops = static_cast<int>(arg_count) + 1;
          break;
        }
        case OpCode::TailCall: {
          if (pc + 5 >= code.size()) return Fail("TAILCALL arg count out of bounds");
          uint8_t arg_count = code[pc + 5];
          if (stack_types.size() < arg_count) return Fail("TAILCALL stack underflow");
          uint32_t func_id = 0;
          if (!ReadU32(code, pc + 1, &func_id)) return Fail("TAILCALL function id out of bounds");
          if (func_id >= module.functions.size()) return Fail("TAILCALL function id out of range");
          uint32_t callee_method = module.functions[func_id].method_id;
          if (callee_method >= module.methods.size()) return Fail("TAILCALL method id out of range");
          uint32_t sig_id = module.methods[callee_method].sig_id;
          if (sig_id >= module.sigs.size()) return Fail("TAILCALL signature id out of range");
          const auto& call_sig = module.sigs[sig_id];
          if (call_sig.param_count > 0 &&
              call_sig.param_type_start + call_sig.param_count > module.param_types.size()) {
            return Fail("TAILCALL signature param types out of range");
          }
          for (int i = static_cast<int>(call_sig.param_count) - 1; i >= 0; --i) {
            ValType got = pop_type();
            ValType expected = ValType::Unknown;
            if (call_sig.param_count > 0) {
              uint32_t type_id = module.param_types[call_sig.param_type_start + static_cast<uint16_t>(i)];
              expected = resolve_type(type_id);
            }
            VerifyResult r = check_type(got, expected, "TAILCALL arg type mismatch");
            if (!r.ok) return r;
          }
          extra_pops = arg_count;
          fall_through = false;
          break;
        }
        case OpCode::ConvI32ToI64: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::I32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I64);
          break;
        }
        case OpCode::ConvI64ToI32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::I64, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ConvI32ToF32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::I32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F32);
          break;
        }
        case OpCode::ConvI32ToF64: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::I32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ConvF32ToI32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ConvF64ToI32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F64, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ConvF32ToF64: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ConvF64ToF32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F64, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F32);
          break;
        }
        case OpCode::Halt:
        case OpCode::Trap:
        case OpCode::Ret:
          if (static_cast<OpCode>(opcode) == OpCode::Ret) {
            if (expect_void) {
              if (!stack_types.empty()) return Fail("return value on void");
            } else {
              if (stack_types.size() != 1) return Fail("return stack size mismatch");
              VerifyResult r = check_type(stack_types.back(), expected_ret, "return type mismatch");
              if (!r.ok) return r;
            }
          }
          fall_through = false;
          break;
        default:
          for (int i = 0; i < info.pops; ++i) pop_type();
          for (int i = 0; i < info.pushes; ++i) push_type(ValType::Unknown);
          break;
      }

      int pop_count = info.pops + extra_pops;
      if (pop_count > 0) {
        if (stack_height - pop_count < 0) return Fail("stack underflow");
        stack_height -= pop_count;
      }
      stack_height += info.pushes + extra_pushes;
      if (static_cast<uint32_t>(stack_height) > func.stack_max) {
        return Fail("stack exceeds max");
      }
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
