#include "jit/compile_policy.h"

#include "interpreter/dispatch.h"
#include "jit/jit_scaffold.h"
#include "opcode.h"

namespace Simple::VM::Jit {
namespace {

using Simple::Byte::OpCode;
using Simple::Byte::TypeKind;
using Simple::VM::Interpreter::ReadU8;
using Simple::VM::Interpreter::ReadU16;
using Simple::VM::Interpreter::ReadU32;

} // namespace

bool CanCompileMethod(const Simple::Byte::SbcModule& module,
                        const Simple::Byte::VerifyResult& verify_result,
                        bool have_meta,
                        size_t func_index,
                        std::vector<uint8_t>& compile_stack) {
    if (func_index >= module.functions.size()) return false;
    if (compile_stack[func_index]) return false;
    struct Guard {
      std::vector<uint8_t>& stack;
      size_t index;
      Guard(std::vector<uint8_t>& stack, size_t index) : stack(stack), index(index) {
        stack[index] = 1;
      }
      ~Guard() { stack[index] = 0; }
    } guard(compile_stack, func_index);
    const auto& func = module.functions[func_index];
    if (func.method_id >= module.methods.size()) return false;
    const auto& method = module.methods[func.method_id];
    if (method.sig_id >= module.sigs.size()) return false;
    const auto& sig = module.sigs[method.sig_id];
    if (sig.param_count > 0) {
      if (sig.param_type_start + sig.param_count > module.param_types.size()) return false;
      if (method.local_count < sig.param_count) return false;
      for (uint16_t i = 0; i < sig.param_count; ++i) {
        uint32_t type_id = module.param_types[sig.param_type_start + i];
        if (type_id >= module.types.size()) return false;
        TypeKind kind = static_cast<TypeKind>(module.types[type_id].kind);
        if (!Simple::VM::Jit::IsValueKind(kind)) return false;
      }
    }
    if (sig.ret_type_id != 0xFFFFFFFFu) {
      if (sig.ret_type_id >= module.types.size()) return false;
      TypeKind ret_kind = static_cast<TypeKind>(module.types[sig.ret_type_id].kind);
      if (!Simple::VM::Jit::IsValueKind(ret_kind)) return false;
    }
    size_t locals_count = 0;
    bool saw_enter = false;
    bool needs_stack_map = false;
    size_t pc = func.code_offset;
    size_t end_pc = func.code_offset + func.code_size;
    while (pc < end_pc) {
      uint8_t op = module.code[pc++];
      switch (static_cast<OpCode>(op)) {
        case OpCode::Enter: {
          if (pc + 2 > end_pc) return false;
          uint16_t locals = ReadU16(module.code, pc);
          if (saw_enter && locals_count != locals) return false;
          locals_count = locals;
          saw_enter = true;
          break;
        }
        case OpCode::Nop:
        case OpCode::Pop:
        case OpCode::Ret:
          break;
        case OpCode::Line: {
          if (pc + 8 > end_pc) return false;
          pc += 8;
          break;
        }
        case OpCode::ProfileStart:
        case OpCode::ProfileEnd: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::ConstI8:
        case OpCode::ConstU8:
        case OpCode::ConstBool: {
          if (pc + 1 > end_pc) return false;
          pc += 1;
          break;
        }
        case OpCode::ConstI16:
        case OpCode::ConstU16:
        case OpCode::ConstChar: {
          if (pc + 2 > end_pc) return false;
          pc += 2;
          break;
        }
        case OpCode::ConstI32:
        case OpCode::ConstU32:
        case OpCode::ConstF32: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::ConstI64:
        case OpCode::ConstU64:
        case OpCode::ConstF64: {
          if (pc + 8 > end_pc) return false;
          pc += 8;
          break;
        }
        case OpCode::ConstNull: {
          needs_stack_map = true;
          break;
        }
        case OpCode::AddI32:
        case OpCode::SubI32:
        case OpCode::MulI32: {
          break;
        }
        case OpCode::DivI32: {
          break;
        }
        case OpCode::ModI32: {
          break;
        }
        case OpCode::NegI32:
        case OpCode::NegI64:
        case OpCode::NegU32:
        case OpCode::NegU64:
        case OpCode::NegI8:
        case OpCode::NegI16:
        case OpCode::NegU8:
        case OpCode::NegU16:
        case OpCode::NegF32:
        case OpCode::NegF64:
        case OpCode::IncI32:
        case OpCode::DecI32:
        case OpCode::IncI64:
        case OpCode::DecI64:
        case OpCode::IncU32:
        case OpCode::DecU32:
        case OpCode::IncU64:
        case OpCode::DecU64:
        case OpCode::IncI8:
        case OpCode::DecI8:
        case OpCode::IncI16:
        case OpCode::DecI16:
        case OpCode::IncU8:
        case OpCode::DecU8:
        case OpCode::IncU16:
        case OpCode::DecU16:
        case OpCode::IncF32:
        case OpCode::DecF32:
        case OpCode::IncF64:
        case OpCode::DecF64: {
          break;
        }
        case OpCode::AddI64:
        case OpCode::SubI64:
        case OpCode::MulI64:
        case OpCode::DivI64:
        case OpCode::ModI64:
        case OpCode::AddU32:
        case OpCode::SubU32:
        case OpCode::MulU32:
        case OpCode::DivU32:
        case OpCode::ModU32:
        case OpCode::AddU64:
        case OpCode::SubU64:
        case OpCode::MulU64:
        case OpCode::DivU64:
        case OpCode::ModU64:
        case OpCode::AddF32:
        case OpCode::SubF32:
        case OpCode::MulF32:
        case OpCode::DivF32:
        case OpCode::AddF64:
        case OpCode::SubF64:
        case OpCode::MulF64:
        case OpCode::DivF64: {
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          break;
        }
        case OpCode::CmpEqU32:
        case OpCode::CmpNeU32:
        case OpCode::CmpLtU32:
        case OpCode::CmpLeU32:
        case OpCode::CmpGtU32:
        case OpCode::CmpGeU32:
        case OpCode::CmpEqI64:
        case OpCode::CmpNeI64:
        case OpCode::CmpLtI64:
        case OpCode::CmpLeI64:
        case OpCode::CmpGtI64:
        case OpCode::CmpGeI64:
        case OpCode::CmpEqU64:
        case OpCode::CmpNeU64:
        case OpCode::CmpLtU64:
        case OpCode::CmpLeU64:
        case OpCode::CmpGtU64:
        case OpCode::CmpGeU64:
        case OpCode::CmpEqF32:
        case OpCode::CmpNeF32:
        case OpCode::CmpLtF32:
        case OpCode::CmpLeF32:
        case OpCode::CmpGtF32:
        case OpCode::CmpGeF32:
        case OpCode::CmpEqF64:
        case OpCode::CmpNeF64:
        case OpCode::CmpLtF64:
        case OpCode::CmpLeF64:
        case OpCode::CmpGtF64:
        case OpCode::CmpGeF64: {
          break;
        }
        case OpCode::BoolNot:
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          break;
        }
        case OpCode::AndI32:
        case OpCode::OrI32:
        case OpCode::XorI32:
        case OpCode::ShlI32:
        case OpCode::ShrI32:
        case OpCode::AndI64:
        case OpCode::OrI64:
        case OpCode::XorI64:
        case OpCode::ShlI64:
        case OpCode::ShrI64: {
          break;
        }
        case OpCode::Dup:
        case OpCode::Dup2:
        case OpCode::Swap:
        case OpCode::Rot: {
          break;
        }
        case OpCode::IsNull:
        case OpCode::RefEq:
        case OpCode::RefNe: {
          needs_stack_map = true;
          break;
        }
        case OpCode::ArrayLen:
        case OpCode::ArrayGetI32:
        case OpCode::ArraySetI32:
        case OpCode::ListLen:
        case OpCode::ListGetI32:
        case OpCode::ListSetI32:
        case OpCode::StringLen: {
          needs_stack_map = true;
          break;
        }
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::Jmp: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::ConvI32ToI64:
        case OpCode::ConvI64ToI32:
        case OpCode::ConvI32ToF32:
        case OpCode::ConvI32ToF64:
        case OpCode::ConvF32ToI32:
        case OpCode::ConvF64ToI32:
        case OpCode::ConvF32ToF64:
        case OpCode::ConvF64ToF32: {
          break;
        }
        case OpCode::LoadLocal: {
          if (!saw_enter || pc + 4 > end_pc) return false;
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals_count) return false;
          break;
        }
        case OpCode::StoreLocal: {
          if (!saw_enter || pc + 4 > end_pc) return false;
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals_count) return false;
          break;
        }
        case OpCode::Call: {
          if (pc + 5 > end_pc) return false;
          uint32_t call_id = ReadU32(module.code, pc);
          uint8_t arg_count = ReadU8(module.code, pc);
          if (call_id >= module.functions.size()) return false;
          if (call_id == func_index) return false;
          if (call_id < module.function_is_import.size() && module.function_is_import[call_id]) return false;
          const auto& target_func = module.functions[call_id];
          if (target_func.method_id >= module.methods.size()) return false;
          const auto& target_method = module.methods[target_func.method_id];
          if (target_method.sig_id >= module.sigs.size()) return false;
          const auto& target_sig = module.sigs[target_method.sig_id];
          if (arg_count != target_sig.param_count) return false;
          if (!CanCompileMethod(module, verify_result, have_meta, call_id, compile_stack)) return false;
          break;
        }
        default:
          return false;
      }
    }
    if (sig.param_count > 0) {
      if (!saw_enter) return false;
      if (locals_count < sig.param_count) return false;
    }
    if (needs_stack_map) {
      if (!have_meta) return false;
      if (func_index >= verify_result.methods.size()) return false;
      if (verify_result.methods[func_index].stack_maps.empty()) return false;
    }
    return true;

}

bool CompilePredicate::operator()(size_t func_index) const {
  if (!module || !verify_result || !compile_stack) return false;
  return CanCompileMethod(*module, *verify_result, have_meta, func_index, *compile_stack);
}

} // namespace Simple::VM::Jit
