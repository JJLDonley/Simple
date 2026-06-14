#include "jit/compiled_runner.h"

#include <sstream>

#include "heap.h"
#include "interpreter/dispatch.h"
#include "jit/failure_format.h"
#include "opcode.h"
#include "runtime/values.h"

namespace Simple::VM::Jit {
namespace {

using Simple::Byte::OpCode;
using Simple::Byte::TypeKind;
using Slot = Simple::VM::Interpreter::Slot;
using Simple::VM::Interpreter::ReadI32;
using Simple::VM::Interpreter::ReadI64;
using Simple::VM::Interpreter::ReadU8;
using Simple::VM::Interpreter::ReadU16;
using Simple::VM::Interpreter::ReadU32;
using Simple::VM::Interpreter::ReadU64;
using Simple::VM::Runtime::BitsToF32;
using Simple::VM::Runtime::BitsToF64;
using Simple::VM::Runtime::F32ToBits;
using Simple::VM::Runtime::F64ToBits;
using Simple::VM::Runtime::IsNullRef;
using Simple::VM::Runtime::PackF32Bits;
using Simple::VM::Runtime::PackF64Bits;
using Simple::VM::Runtime::PackI32;
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::UnpackI32;
using Simple::VM::Runtime::UnpackI64;
using Simple::VM::Runtime::UnpackRef;
using Simple::VM::Runtime::UnpackU32Bits;
using Simple::VM::Runtime::UnpackU64Bits;
constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

} // namespace

bool RunCompiledFunction(CompiledRunContext& context,
                         size_t func_index,
                         const std::vector<Slot>& args,
                         Slot& out_ret,
                         bool& out_has_ret,
                         std::string& error) {
  if (!context.module || !context.heap || !context.jit_tiers || !context.update_tier || !context.can_compile ||
      !context.jit_compiled_exec_counts || !context.jit_tier1_exec_counts) {
    error = "JIT compiled context invalid";
    return false;
  }
  const auto& module = *context.module;
  Heap& heap = *context.heap;
  const auto& jit_tiers = *context.jit_tiers;
  TierUpdater& update_tier = *context.update_tier;
  CompilePredicate& can_compile_func = *context.can_compile;
  auto& jit_compiled_exec_counts = *context.jit_compiled_exec_counts;
  auto& jit_tier1_exec_counts = *context.jit_tier1_exec_counts;

    if (func_index >= module.functions.size()) {
      std::ostringstream out;
      out << "JIT compiled invalid function id op 0xFF Unknown pc 0";
      error = out.str();
      return false;
    }
    const auto& func = module.functions[func_index];
    if (func.method_id >= module.methods.size()) {
      error = "JIT compiled invalid method id";
      return false;
    }
    const auto& method = module.methods[func.method_id];
    if (method.sig_id >= module.sigs.size()) {
      error = "JIT compiled invalid signature id";
      return false;
    }
    const auto& sig = module.sigs[method.sig_id];
    const uint16_t param_count = sig.param_count;
    if (args.size() != param_count) {
      error = "JIT compiled arg count mismatch";
      return false;
    }
    size_t pc = func.code_offset;
    size_t end_pc = func.code_offset + func.code_size;
    std::vector<Slot> local_stack;
    std::vector<Slot> locals;
    std::vector<Slot> call_args;
    bool saw_enter = false;
    bool skip_nops = (jit_tiers[func_index] == JitTier::Tier1);
    Simple::VM::Jit::CompiledFailureReporter fail_compiled;
    fail_compiled.module = &module;
    fail_compiled.function = &func;
    fail_compiled.error = &error;
    while (pc < end_pc) {
      uint8_t op = module.code[pc++];
      size_t inst_pc = pc - 1;
      switch (static_cast<OpCode>(op)) {
        case OpCode::Enter: {
          if (pc + 2 > end_pc) {
            return fail_compiled("JIT compiled ENTER out of bounds", op, inst_pc);
          }
          uint16_t locals_count = ReadU16(module.code, pc);
          if (!saw_enter) {
            locals.assign(locals_count, 0);
            if (param_count > 0) {
              if (locals_count < param_count) {
                return fail_compiled("JIT compiled locals < param count", op, inst_pc);
              }
              for (uint16_t i = 0; i < param_count; ++i) {
                locals[static_cast<size_t>(i)] = args[static_cast<size_t>(i)];
              }
            }
            saw_enter = true;
          } else if (locals.size() != locals_count) {
            return fail_compiled("JIT compiled locals mismatch", op, inst_pc);
          }
          break;
        }
        case OpCode::Nop:
          if (skip_nops) {
            break;
          }
          break;
        case OpCode::Line: {
          if (pc + 8 > end_pc) {
            return fail_compiled("JIT compiled LINE out of bounds", op, inst_pc);
          }
          ReadU32(module.code, pc);
          ReadU32(module.code, pc);
          break;
        }
        case OpCode::ProfileStart:
        case OpCode::ProfileEnd: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled PROFILE out of bounds", op, inst_pc);
          }
          ReadU32(module.code, pc);
          break;
        }
        case OpCode::Dup: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled DUP underflow", op, inst_pc);
          }
          local_stack.push_back(local_stack.back());
          break;
        }
        case OpCode::Dup2: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled DUP2 underflow", op, inst_pc);
          }
          Slot a = local_stack[local_stack.size() - 2];
          Slot b = local_stack[local_stack.size() - 1];
          local_stack.push_back(a);
          local_stack.push_back(b);
          break;
        }
        case OpCode::Swap: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled SWAP underflow", op, inst_pc);
          }
          Slot a = local_stack[local_stack.size() - 1];
          Slot b = local_stack[local_stack.size() - 2];
          local_stack[local_stack.size() - 1] = b;
          local_stack[local_stack.size() - 2] = a;
          break;
        }
        case OpCode::Rot: {
          if (local_stack.size() < 3) {
            return fail_compiled("JIT compiled ROT underflow", op, inst_pc);
          }
          Slot c = local_stack[local_stack.size() - 1];
          Slot b = local_stack[local_stack.size() - 2];
          Slot a = local_stack[local_stack.size() - 3];
          local_stack[local_stack.size() - 3] = b;
          local_stack[local_stack.size() - 2] = c;
          local_stack[local_stack.size() - 1] = a;
          break;
        }
        case OpCode::ConstI8: {
          if (pc + 1 > end_pc) {
            return fail_compiled("JIT compiled CONST_I8 out of bounds", op, inst_pc);
          }
          int8_t value = static_cast<int8_t>(ReadU8(module.code, pc));
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::ConstU8: {
          if (pc + 1 > end_pc) {
            return fail_compiled("JIT compiled CONST_U8 out of bounds", op, inst_pc);
          }
          uint8_t value = ReadU8(module.code, pc);
          local_stack.push_back(PackI32(static_cast<int32_t>(value)));
          break;
        }
        case OpCode::ConstBool: {
          if (pc + 1 > end_pc) {
            return fail_compiled("JIT compiled CONST_BOOL out of bounds", op, inst_pc);
          }
          uint8_t value = ReadU8(module.code, pc);
          local_stack.push_back(PackI32(value ? 1 : 0));
          break;
        }
        case OpCode::ConstI16: {
          if (pc + 2 > end_pc) {
            return fail_compiled("JIT compiled CONST_I16 out of bounds", op, inst_pc);
          }
          int16_t value = static_cast<int16_t>(ReadU16(module.code, pc));
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::ConstU16: {
          if (pc + 2 > end_pc) {
            return fail_compiled("JIT compiled CONST_U16 out of bounds", op, inst_pc);
          }
          uint16_t value = ReadU16(module.code, pc);
          local_stack.push_back(PackI32(static_cast<int32_t>(value)));
          break;
        }
        case OpCode::ConstChar: {
          if (pc + 2 > end_pc) {
            return fail_compiled("JIT compiled CONST_CHAR out of bounds", op, inst_pc);
          }
          uint16_t value = ReadU16(module.code, pc);
          local_stack.push_back(PackI32(static_cast<int32_t>(value)));
          break;
        }
        case OpCode::ConstI32: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled CONST_I32 out of bounds", op, inst_pc);
          }
          int32_t value = ReadI32(module.code, pc);
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::ConstU32: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled CONST_U32 out of bounds", op, inst_pc);
          }
          uint32_t value = ReadU32(module.code, pc);
          local_stack.push_back(PackI32(static_cast<int32_t>(value)));
          break;
        }
        case OpCode::ConstI64: {
          if (pc + 8 > end_pc) {
            return fail_compiled("JIT compiled CONST_I64 out of bounds", op, inst_pc);
          }
          int64_t value = ReadI64(module.code, pc);
          local_stack.push_back(PackI64(value));
          break;
        }
        case OpCode::ConstU64: {
          if (pc + 8 > end_pc) {
            return fail_compiled("JIT compiled CONST_U64 out of bounds", op, inst_pc);
          }
          uint64_t value = ReadU64(module.code, pc);
          local_stack.push_back(PackI64(static_cast<int64_t>(value)));
          break;
        }
        case OpCode::ConstF32: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled CONST_F32 out of bounds", op, inst_pc);
          }
          uint32_t bits = ReadU32(module.code, pc);
          local_stack.push_back(PackF32Bits(bits));
          break;
        }
        case OpCode::ConstF64: {
          if (pc + 8 > end_pc) {
            return fail_compiled("JIT compiled CONST_F64 out of bounds", op, inst_pc);
          }
          uint64_t bits = ReadU64(module.code, pc);
          local_stack.push_back(PackF64Bits(bits));
          break;
        }
        case OpCode::ConstNull: {
          local_stack.push_back(PackRef(kNullRef));
          break;
        }
        case OpCode::AddI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled ADD_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          local_stack.push_back(PackI32(static_cast<int32_t>(a + b)));
          break;
        }
        case OpCode::SubI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled SUB_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          local_stack.push_back(PackI32(static_cast<int32_t>(a - b)));
          break;
        }
        case OpCode::MulI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled MUL_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          local_stack.push_back(PackI32(static_cast<int32_t>(a * b)));
          break;
        }
        case OpCode::DivI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled DIV_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          if (b == 0) {
            return fail_compiled("JIT compiled DIV_I32 by zero", op, inst_pc);
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(a / b)));
          break;
        }
        case OpCode::ModI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled MOD_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          if (b == 0) {
            return fail_compiled("JIT compiled MOD_I32 by zero", op, inst_pc);
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(a % b)));
          break;
        }
        case OpCode::NegI32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_I32 underflow", op, inst_pc);
          }
          int32_t a = UnpackI32(local_stack.back());
          local_stack.back() = PackI32(static_cast<int32_t>(-a));
          break;
        }
        case OpCode::IncI32:
        case OpCode::DecI32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_I32 underflow", op, inst_pc);
          }
          int32_t a = UnpackI32(local_stack.back());
          a = (static_cast<OpCode>(op) == OpCode::IncI32) ? (a + 1) : (a - 1);
          local_stack.back() = PackI32(a);
          break;
        }
        case OpCode::AddU32:
        case OpCode::SubU32:
        case OpCode::MulU32:
        case OpCode::DivU32:
        case OpCode::ModU32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled U32 binop underflow", op, inst_pc);
          }
          uint32_t b = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          local_stack.pop_back();
          uint32_t a = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          local_stack.pop_back();
          uint32_t out = 0;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AddU32:
              out = a + b;
              break;
            case OpCode::SubU32:
              out = a - b;
              break;
            case OpCode::MulU32:
              out = a * b;
              break;
            case OpCode::DivU32:
              out = (b == 0) ? 0u : (a / b);
              break;
            case OpCode::ModU32:
              out = (b == 0) ? 0u : (a % b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(out)));
          break;
        }
        case OpCode::NegU32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_U32 underflow", op, inst_pc);
          }
          uint32_t a = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          uint32_t out = 0u - a;
          local_stack.back() = PackI32(static_cast<int32_t>(out));
          break;
        }
        case OpCode::IncU32:
        case OpCode::DecU32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_U32 underflow", op, inst_pc);
          }
          uint32_t a = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncU32) ? (a + 1u) : (a - 1u);
          local_stack.back() = PackI32(static_cast<int32_t>(a));
          break;
        }
        case OpCode::AddI64:
        case OpCode::SubI64:
        case OpCode::MulI64:
        case OpCode::DivI64:
        case OpCode::ModI64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled I64 binop underflow", op, inst_pc);
          }
          int64_t b = UnpackI64(local_stack.back());
          local_stack.pop_back();
          int64_t a = UnpackI64(local_stack.back());
          local_stack.pop_back();
          int64_t out = 0;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AddI64:
              out = a + b;
              break;
            case OpCode::SubI64:
              out = a - b;
              break;
            case OpCode::MulI64:
              out = a * b;
              break;
            case OpCode::DivI64:
              out = (b == 0) ? 0 : (a / b);
              break;
            case OpCode::ModI64:
              out = (b == 0) ? 0 : (a % b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI64(out));
          break;
        }
        case OpCode::NegI64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_I64 underflow", op, inst_pc);
          }
          int64_t a = UnpackI64(local_stack.back());
          local_stack.back() = PackI64(-a);
          break;
        }
        case OpCode::IncI64:
        case OpCode::DecI64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_I64 underflow", op, inst_pc);
          }
          int64_t a = UnpackI64(local_stack.back());
          a = (static_cast<OpCode>(op) == OpCode::IncI64) ? (a + 1) : (a - 1);
          local_stack.back() = PackI64(a);
          break;
        }
        case OpCode::AddU64:
        case OpCode::SubU64:
        case OpCode::MulU64:
        case OpCode::DivU64:
        case OpCode::ModU64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled U64 binop underflow", op, inst_pc);
          }
          uint64_t b = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          local_stack.pop_back();
          uint64_t a = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          local_stack.pop_back();
          uint64_t out = 0;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AddU64:
              out = a + b;
              break;
            case OpCode::SubU64:
              out = a - b;
              break;
            case OpCode::MulU64:
              out = a * b;
              break;
            case OpCode::DivU64:
              out = (b == 0) ? 0u : (a / b);
              break;
            case OpCode::ModU64:
              out = (b == 0) ? 0u : (a % b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI64(static_cast<int64_t>(out)));
          break;
        }
        case OpCode::NegU64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_U64 underflow", op, inst_pc);
          }
          uint64_t a = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          uint64_t out = 0u - a;
          local_stack.back() = PackI64(static_cast<int64_t>(out));
          break;
        }
        case OpCode::IncU64:
        case OpCode::DecU64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_U64 underflow", op, inst_pc);
          }
          uint64_t a = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncU64) ? (a + 1u) : (a - 1u);
          local_stack.back() = PackI64(static_cast<int64_t>(a));
          break;
        }
        case OpCode::AddF32:
        case OpCode::SubF32:
        case OpCode::MulF32:
        case OpCode::DivF32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled F32 binop underflow", op, inst_pc);
          }
          float b = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.pop_back();
          float a = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.pop_back();
          float out = 0.0f;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AddF32:
              out = a + b;
              break;
            case OpCode::SubF32:
              out = a - b;
              break;
            case OpCode::MulF32:
              out = a * b;
              break;
            case OpCode::DivF32:
              out = (b == 0.0f) ? 0.0f : (a / b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackF32Bits(F32ToBits(out)));
          break;
        }
        case OpCode::NegF32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_F32 underflow", op, inst_pc);
          }
          float a = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.back() = PackF32Bits(F32ToBits(-a));
          break;
        }
        case OpCode::IncF32:
        case OpCode::DecF32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_F32 underflow", op, inst_pc);
          }
          float a = BitsToF32(UnpackU32Bits(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncF32) ? (a + 1.0f) : (a - 1.0f);
          local_stack.back() = PackF32Bits(F32ToBits(a));
          break;
        }
        case OpCode::AddF64:
        case OpCode::SubF64:
        case OpCode::MulF64:
        case OpCode::DivF64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled F64 binop underflow", op, inst_pc);
          }
          double b = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.pop_back();
          double a = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.pop_back();
          double out = 0.0;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AddF64:
              out = a + b;
              break;
            case OpCode::SubF64:
              out = a - b;
              break;
            case OpCode::MulF64:
              out = a * b;
              break;
            case OpCode::DivF64:
              out = (b == 0.0) ? 0.0 : (a / b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackF64Bits(F64ToBits(out)));
          break;
        }
        case OpCode::NegF64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_F64 underflow", op, inst_pc);
          }
          double a = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.back() = PackF64Bits(F64ToBits(-a));
          break;
        }
        case OpCode::IncF64:
        case OpCode::DecF64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_F64 underflow", op, inst_pc);
          }
          double a = BitsToF64(UnpackU64Bits(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncF64) ? (a + 1.0) : (a - 1.0);
          local_stack.back() = PackF64Bits(F64ToBits(a));
          break;
        }
        case OpCode::NegI8: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_I8 underflow", op, inst_pc);
          }
          int8_t a = static_cast<int8_t>(UnpackI32(local_stack.back()));
          local_stack.back() = PackI32(static_cast<int8_t>(-a));
          break;
        }
        case OpCode::NegI16: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_I16 underflow", op, inst_pc);
          }
          int16_t a = static_cast<int16_t>(UnpackI32(local_stack.back()));
          local_stack.back() = PackI32(static_cast<int16_t>(-a));
          break;
        }
        case OpCode::NegU8: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_U8 underflow", op, inst_pc);
          }
          uint8_t a = static_cast<uint8_t>(UnpackI32(local_stack.back()));
          uint8_t out = static_cast<uint8_t>(0u - a);
          local_stack.back() = PackI32(static_cast<int32_t>(out));
          break;
        }
        case OpCode::NegU16: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled NEG_U16 underflow", op, inst_pc);
          }
          uint16_t a = static_cast<uint16_t>(UnpackI32(local_stack.back()));
          uint16_t out = static_cast<uint16_t>(0u - a);
          local_stack.back() = PackI32(static_cast<int32_t>(out));
          break;
        }
        case OpCode::IncI8:
        case OpCode::DecI8: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_I8 underflow", op, inst_pc);
          }
          int8_t a = static_cast<int8_t>(UnpackI32(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncI8) ? static_cast<int8_t>(a + 1)
                                                         : static_cast<int8_t>(a - 1);
          local_stack.back() = PackI32(a);
          break;
        }
        case OpCode::IncI16:
        case OpCode::DecI16: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_I16 underflow", op, inst_pc);
          }
          int16_t a = static_cast<int16_t>(UnpackI32(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncI16) ? static_cast<int16_t>(a + 1)
                                                          : static_cast<int16_t>(a - 1);
          local_stack.back() = PackI32(a);
          break;
        }
        case OpCode::IncU8:
        case OpCode::DecU8: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_U8 underflow", op, inst_pc);
          }
          uint8_t a = static_cast<uint8_t>(UnpackI32(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncU8) ? static_cast<uint8_t>(a + 1u)
                                                         : static_cast<uint8_t>(a - 1u);
          local_stack.back() = PackI32(static_cast<int32_t>(a));
          break;
        }
        case OpCode::IncU16:
        case OpCode::DecU16: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled INC/DEC_U16 underflow", op, inst_pc);
          }
          uint16_t a = static_cast<uint16_t>(UnpackI32(local_stack.back()));
          a = (static_cast<OpCode>(op) == OpCode::IncU16) ? static_cast<uint16_t>(a + 1u)
                                                          : static_cast<uint16_t>(a - 1u);
          local_stack.back() = PackI32(static_cast<int32_t>(a));
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled CMP_I32 underflow", op, inst_pc);
          }
          Slot rhs = local_stack.back();
          local_stack.pop_back();
          Slot lhs = local_stack.back();
          local_stack.pop_back();
          int32_t a = UnpackI32(lhs);
          int32_t b = UnpackI32(rhs);
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqI32:
              result = (a == b);
              break;
            case OpCode::CmpNeI32:
              result = (a != b);
              break;
            case OpCode::CmpLtI32:
              result = (a < b);
              break;
            case OpCode::CmpLeI32:
              result = (a <= b);
              break;
            case OpCode::CmpGtI32:
              result = (a > b);
              break;
            case OpCode::CmpGeI32:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::CmpEqU32:
        case OpCode::CmpNeU32:
        case OpCode::CmpLtU32:
        case OpCode::CmpLeU32:
        case OpCode::CmpGtU32:
        case OpCode::CmpGeU32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled CMP_U32 underflow", op, inst_pc);
          }
          uint32_t b = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          local_stack.pop_back();
          uint32_t a = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          local_stack.pop_back();
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqU32:
              result = (a == b);
              break;
            case OpCode::CmpNeU32:
              result = (a != b);
              break;
            case OpCode::CmpLtU32:
              result = (a < b);
              break;
            case OpCode::CmpLeU32:
              result = (a <= b);
              break;
            case OpCode::CmpGtU32:
              result = (a > b);
              break;
            case OpCode::CmpGeU32:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::CmpEqI64:
        case OpCode::CmpNeI64:
        case OpCode::CmpLtI64:
        case OpCode::CmpLeI64:
        case OpCode::CmpGtI64:
        case OpCode::CmpGeI64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled CMP_I64 underflow", op, inst_pc);
          }
          int64_t b = UnpackI64(local_stack.back());
          local_stack.pop_back();
          int64_t a = UnpackI64(local_stack.back());
          local_stack.pop_back();
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqI64:
              result = (a == b);
              break;
            case OpCode::CmpNeI64:
              result = (a != b);
              break;
            case OpCode::CmpLtI64:
              result = (a < b);
              break;
            case OpCode::CmpLeI64:
              result = (a <= b);
              break;
            case OpCode::CmpGtI64:
              result = (a > b);
              break;
            case OpCode::CmpGeI64:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::CmpEqU64:
        case OpCode::CmpNeU64:
        case OpCode::CmpLtU64:
        case OpCode::CmpLeU64:
        case OpCode::CmpGtU64:
        case OpCode::CmpGeU64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled CMP_U64 underflow", op, inst_pc);
          }
          uint64_t b = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          local_stack.pop_back();
          uint64_t a = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          local_stack.pop_back();
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqU64:
              result = (a == b);
              break;
            case OpCode::CmpNeU64:
              result = (a != b);
              break;
            case OpCode::CmpLtU64:
              result = (a < b);
              break;
            case OpCode::CmpLeU64:
              result = (a <= b);
              break;
            case OpCode::CmpGtU64:
              result = (a > b);
              break;
            case OpCode::CmpGeU64:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::CmpEqF32:
        case OpCode::CmpNeF32:
        case OpCode::CmpLtF32:
        case OpCode::CmpLeF32:
        case OpCode::CmpGtF32:
        case OpCode::CmpGeF32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled CMP_F32 underflow", op, inst_pc);
          }
          float b = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.pop_back();
          float a = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.pop_back();
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqF32:
              result = (a == b);
              break;
            case OpCode::CmpNeF32:
              result = (a != b);
              break;
            case OpCode::CmpLtF32:
              result = (a < b);
              break;
            case OpCode::CmpLeF32:
              result = (a <= b);
              break;
            case OpCode::CmpGtF32:
              result = (a > b);
              break;
            case OpCode::CmpGeF32:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::CmpEqF64:
        case OpCode::CmpNeF64:
        case OpCode::CmpLtF64:
        case OpCode::CmpLeF64:
        case OpCode::CmpGtF64:
        case OpCode::CmpGeF64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled CMP_F64 underflow", op, inst_pc);
          }
          double b = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.pop_back();
          double a = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.pop_back();
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqF64:
              result = (a == b);
              break;
            case OpCode::CmpNeF64:
              result = (a != b);
              break;
            case OpCode::CmpLtF64:
              result = (a < b);
              break;
            case OpCode::CmpLeF64:
              result = (a <= b);
              break;
            case OpCode::CmpGtF64:
              result = (a > b);
              break;
            case OpCode::CmpGeF64:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::BoolNot: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled BOOL_NOT underflow", op, inst_pc);
          }
          Slot v = local_stack.back();
          local_stack.pop_back();
          local_stack.push_back(PackI32(UnpackI32(v) == 0 ? 1 : 0));
          break;
        }
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled BOOL binop underflow", op, inst_pc);
          }
          Slot rhs = local_stack.back();
          local_stack.pop_back();
          Slot lhs = local_stack.back();
          local_stack.pop_back();
          bool result = false;
          if (static_cast<OpCode>(op) == OpCode::BoolAnd) {
            result = (UnpackI32(lhs) != 0) && (UnpackI32(rhs) != 0);
          } else {
            result = (UnpackI32(lhs) != 0) || (UnpackI32(rhs) != 0);
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::AndI32:
        case OpCode::OrI32:
        case OpCode::XorI32:
        case OpCode::ShlI32:
        case OpCode::ShrI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled I32 bitop underflow", op, inst_pc);
          }
          uint32_t b = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          local_stack.pop_back();
          uint32_t a = static_cast<uint32_t>(UnpackI32(local_stack.back()));
          local_stack.pop_back();
          uint32_t out = 0;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AndI32:
              out = a & b;
              break;
            case OpCode::OrI32:
              out = a | b;
              break;
            case OpCode::XorI32:
              out = a ^ b;
              break;
            case OpCode::ShlI32:
              out = a << (b & 31u);
              break;
            case OpCode::ShrI32:
              out = a >> (b & 31u);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(out)));
          break;
        }
        case OpCode::AndI64:
        case OpCode::OrI64:
        case OpCode::XorI64:
        case OpCode::ShlI64:
        case OpCode::ShrI64: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled I64 bitop underflow", op, inst_pc);
          }
          uint64_t b = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          local_stack.pop_back();
          uint64_t a = static_cast<uint64_t>(UnpackI64(local_stack.back()));
          local_stack.pop_back();
          uint64_t out = 0;
          switch (static_cast<OpCode>(op)) {
            case OpCode::AndI64:
              out = a & b;
              break;
            case OpCode::OrI64:
              out = a | b;
              break;
            case OpCode::XorI64:
              out = a ^ b;
              break;
            case OpCode::ShlI64:
              out = a << (b & 63u);
              break;
            case OpCode::ShrI64:
              out = a >> (b & 63u);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI64(static_cast<int64_t>(out)));
          break;
        }
        case OpCode::IsNull: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled IS_NULL underflow", op, inst_pc);
          }
          Slot v = local_stack.back();
          local_stack.back() = PackI32(IsNullRef(v) ? 1 : 0);
          break;
        }
        case OpCode::RefEq:
        case OpCode::RefNe: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled REF_EQ underflow", op, inst_pc);
          }
          Slot b = local_stack.back();
          local_stack.pop_back();
          Slot a = local_stack.back();
          local_stack.pop_back();
          bool out = (a == b);
          if (static_cast<OpCode>(op) == OpCode::RefNe) out = !out;
          local_stack.push_back(PackI32(out ? 1 : 0));
          break;
        }
        case OpCode::ArrayLen: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled ARRAY_LEN underflow", op, inst_pc);
          }
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled ARRAY_LEN on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::Array) {
            return fail_compiled("JIT compiled ARRAY_LEN on non-array", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          local_stack.push_back(PackI32(static_cast<int32_t>(length)));
          break;
        }
        case OpCode::ArrayGetI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled ARRAY_GET underflow", op, inst_pc);
          }
          Slot idx_val = local_stack.back();
          local_stack.pop_back();
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled ARRAY_GET on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::Array) {
            return fail_compiled("JIT compiled ARRAY_GET on non-array", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          int32_t index = UnpackI32(idx_val);
          if (index < 0 || static_cast<uint32_t>(index) >= length) {
            return fail_compiled("JIT compiled ARRAY_GET out of bounds", op, inst_pc);
          }
          size_t offset = 4 + static_cast<size_t>(index) * 4;
          int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::ArraySetI32: {
          if (local_stack.size() < 3) {
            return fail_compiled("JIT compiled ARRAY_SET underflow", op, inst_pc);
          }
          Slot value = local_stack.back();
          local_stack.pop_back();
          Slot idx_val = local_stack.back();
          local_stack.pop_back();
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled ARRAY_SET on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::Array) {
            return fail_compiled("JIT compiled ARRAY_SET on non-array", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          int32_t index = UnpackI32(idx_val);
          if (index < 0 || static_cast<uint32_t>(index) >= length) {
            return fail_compiled("JIT compiled ARRAY_SET out of bounds", op, inst_pc);
          }
          size_t offset = 4 + static_cast<size_t>(index) * 4;
          WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
          break;
        }
        case OpCode::ListLen: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled LIST_LEN underflow", op, inst_pc);
          }
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled LIST_LEN on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::List) {
            return fail_compiled("JIT compiled LIST_LEN on non-list", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          local_stack.push_back(PackI32(static_cast<int32_t>(length)));
          break;
        }
        case OpCode::ListGetI32: {
          if (local_stack.size() < 2) {
            return fail_compiled("JIT compiled LIST_GET underflow", op, inst_pc);
          }
          Slot idx_val = local_stack.back();
          local_stack.pop_back();
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled LIST_GET on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::List) {
            return fail_compiled("JIT compiled LIST_GET on non-list", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          int32_t index = UnpackI32(idx_val);
          if (index < 0 || static_cast<uint32_t>(index) >= length) {
            return fail_compiled("JIT compiled LIST_GET out of bounds", op, inst_pc);
          }
          size_t offset = 8 + static_cast<size_t>(index) * 4;
          int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::ListSetI32: {
          if (local_stack.size() < 3) {
            return fail_compiled("JIT compiled LIST_SET underflow", op, inst_pc);
          }
          Slot value = local_stack.back();
          local_stack.pop_back();
          Slot idx_val = local_stack.back();
          local_stack.pop_back();
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled LIST_SET on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::List) {
            return fail_compiled("JIT compiled LIST_SET on non-list", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          int32_t index = UnpackI32(idx_val);
          if (index < 0 || static_cast<uint32_t>(index) >= length) {
            return fail_compiled("JIT compiled LIST_SET out of bounds", op, inst_pc);
          }
          size_t offset = 8 + static_cast<size_t>(index) * 4;
          WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
          break;
        }
        case OpCode::StringLen: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled STRING_LEN underflow", op, inst_pc);
          }
          Slot v = local_stack.back();
          local_stack.pop_back();
          if (IsNullRef(v)) {
            return fail_compiled("JIT compiled STRING_LEN on non-ref", op, inst_pc);
          }
          HeapObject* obj = heap.Get(UnpackRef(v));
          if (!obj || obj->header.kind != ObjectKind::String) {
            return fail_compiled("JIT compiled STRING_LEN on non-string", op, inst_pc);
          }
          uint32_t length = ReadU32Payload(obj->payload, 0);
          local_stack.push_back(PackI32(static_cast<int32_t>(length)));
          break;
        }
        case OpCode::Call: {
          if (pc + 5 > end_pc) {
            return fail_compiled("JIT compiled CALL out of bounds", op, inst_pc);
          }
          uint32_t func_id = ReadU32(module.code, pc);
          uint8_t arg_count = ReadU8(module.code, pc);
          if (func_id >= module.functions.size()) {
            return fail_compiled("JIT compiled CALL invalid function id", op, inst_pc);
          }
          const auto& target_func = module.functions[func_id];
          if (target_func.method_id >= module.methods.size()) {
            return fail_compiled("JIT compiled CALL invalid method id", op, inst_pc);
          }
          const auto& target_method = module.methods[target_func.method_id];
          if (target_method.sig_id >= module.sigs.size()) {
            return fail_compiled("JIT compiled CALL invalid signature id", op, inst_pc);
          }
          const auto& target_sig = module.sigs[target_method.sig_id];
          if (arg_count != target_sig.param_count) {
            return fail_compiled("JIT compiled CALL arg count mismatch", op, inst_pc);
          }
          if (local_stack.size() < arg_count) {
            return fail_compiled("JIT compiled CALL underflow", op, inst_pc);
          }
          if (!can_compile_func(func_id)) {
            return fail_compiled("JIT compiled CALL callee unsupported", op, inst_pc);
          }
          call_args.resize(arg_count);
          for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
            call_args[static_cast<size_t>(i)] = local_stack.back();
            local_stack.pop_back();
          }
          update_tier(func_id);
          jit_compiled_exec_counts[func_id] += 1;
          if (jit_tiers[func_id] == JitTier::Tier1) {
            jit_tier1_exec_counts[func_id] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!RunCompiledFunction(context, func_id, call_args, ret, has_ret, error)) {
            return fail_compiled(error.c_str(), op, inst_pc);
          }
          if (has_ret) {
            local_stack.push_back(ret);
          }
          break;
        }
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled JMP out of bounds", op, inst_pc);
          }
          int32_t rel = ReadI32(module.code, pc);
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled JMP underflow", op, inst_pc);
          }
          Slot cond = local_stack.back();
          local_stack.pop_back();
          bool take = UnpackI32(cond) != 0;
          if (static_cast<OpCode>(op) == OpCode::JmpFalse) {
            take = !take;
          }
          if (take) {
            int64_t next = static_cast<int64_t>(pc) + rel;
            if (next < static_cast<int64_t>(func.code_offset) || next > static_cast<int64_t>(end_pc)) {
              std::ostringstream out;
              out << "JIT compiled JMP out of bounds rel=" << rel << " target=" << next;
              return fail_compiled(out.str().c_str(), op, inst_pc);
            }
            pc = static_cast<size_t>(next);
          }
          break;
        }
        case OpCode::Jmp: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled JMP out of bounds", op, inst_pc);
          }
          int32_t rel = ReadI32(module.code, pc);
          int64_t next = static_cast<int64_t>(pc) + rel;
          if (next < static_cast<int64_t>(func.code_offset) || next > static_cast<int64_t>(end_pc)) {
            std::ostringstream out;
            out << "JIT compiled JMP out of bounds rel=" << rel << " target=" << next;
            return fail_compiled(out.str().c_str(), op, inst_pc);
          }
          pc = static_cast<size_t>(next);
          break;
        }
        case OpCode::LoadLocal: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled LOAD_LOCAL out of bounds", op, inst_pc);
          }
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals.size()) {
            return fail_compiled("JIT compiled LOAD_LOCAL invalid index", op, inst_pc);
          }
          local_stack.push_back(locals[idx]);
          break;
        }
        case OpCode::StoreLocal: {
          if (pc + 4 > end_pc) {
            return fail_compiled("JIT compiled STORE_LOCAL out of bounds", op, inst_pc);
          }
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals.size()) {
            return fail_compiled("JIT compiled STORE_LOCAL invalid index", op, inst_pc);
          }
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled STORE_LOCAL underflow", op, inst_pc);
          }
          locals[idx] = local_stack.back();
          local_stack.pop_back();
          break;
        }
        case OpCode::ConvI32ToI64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_I32_TO_I64 underflow", op, inst_pc);
          }
          int32_t v = UnpackI32(local_stack.back());
          local_stack.back() = PackI64(static_cast<int64_t>(v));
          break;
        }
        case OpCode::ConvI64ToI32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_I64_TO_I32 underflow", op, inst_pc);
          }
          int64_t v = UnpackI64(local_stack.back());
          local_stack.back() = PackI32(static_cast<int32_t>(v));
          break;
        }
        case OpCode::ConvI32ToF32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_I32_TO_F32 underflow", op, inst_pc);
          }
          int32_t v = UnpackI32(local_stack.back());
          local_stack.back() = PackF32Bits(F32ToBits(static_cast<float>(v)));
          break;
        }
        case OpCode::ConvI32ToF64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_I32_TO_F64 underflow", op, inst_pc);
          }
          int32_t v = UnpackI32(local_stack.back());
          local_stack.back() = PackF64Bits(F64ToBits(static_cast<double>(v)));
          break;
        }
        case OpCode::ConvF32ToI32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_F32_TO_I32 underflow", op, inst_pc);
          }
          float v = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.back() = PackI32(static_cast<int32_t>(v));
          break;
        }
        case OpCode::ConvF64ToI32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_F64_TO_I32 underflow", op, inst_pc);
          }
          double v = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.back() = PackI32(static_cast<int32_t>(v));
          break;
        }
        case OpCode::ConvF32ToF64: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_F32_TO_F64 underflow", op, inst_pc);
          }
          float v = BitsToF32(UnpackU32Bits(local_stack.back()));
          local_stack.back() = PackF64Bits(F64ToBits(static_cast<double>(v)));
          break;
        }
        case OpCode::ConvF64ToF32: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled CONV_F64_TO_F32 underflow", op, inst_pc);
          }
          double v = BitsToF64(UnpackU64Bits(local_stack.back()));
          local_stack.back() = PackF32Bits(F32ToBits(static_cast<float>(v)));
          break;
        }
        case OpCode::Pop: {
          if (local_stack.empty()) {
            return fail_compiled("JIT compiled POP underflow", op, inst_pc);
          }
          local_stack.pop_back();
          break;
        }
        case OpCode::Ret: {
          out_has_ret = false;
          if (!local_stack.empty()) {
            out_ret = local_stack.back();
            out_has_ret = true;
          }
          return true;
        }
        default:
          return fail_compiled("JIT compiled unsupported opcode", op, inst_pc);
      }
    }
    return fail_compiled("JIT compiled missing RET", static_cast<uint8_t>(OpCode::Ret), end_pc);
  
}

} // namespace Simple::VM::Jit
