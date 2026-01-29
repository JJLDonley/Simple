#include "vm.h"

#include <cstdint>
#include <vector>

#include "opcode.h"

namespace simplevm {
namespace {

enum class ValueKind {
  I32,
  Bool,
  None,
};

struct Value {
  ValueKind kind = ValueKind::None;
  int64_t i64 = 0;
};

struct Frame {
  size_t func_index = 0;
  size_t return_pc = 0;
  size_t stack_base = 0;
  std::vector<Value> locals;
};

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return static_cast<int32_t>(v);
}

uint32_t ReadU32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return v;
}

uint16_t ReadU16(const std::vector<uint8_t>& code, size_t& pc) {
  uint16_t v = static_cast<uint16_t>(code[pc]) |
               (static_cast<uint16_t>(code[pc + 1]) << 8);
  pc += 2;
  return v;
}

uint8_t ReadU8(const std::vector<uint8_t>& code, size_t& pc) {
  return code[pc++];
}

Value Pop(std::vector<Value>& stack) {
  Value v = stack.back();
  stack.pop_back();
  return v;
}

void Push(std::vector<Value>& stack, Value v) {
  stack.push_back(v);
}

ExecResult Trap(const std::string& message) {
  ExecResult result;
  result.status = ExecStatus::Trapped;
  result.error = message;
  return result;
}

} // namespace

ExecResult ExecuteModule(const SbcModule& module) {
  if (module.functions.empty()) return Trap("no functions to execute");
  if (module.header.entry_method_id == 0xFFFFFFFFu) return Trap("no entry point");

  std::vector<Value> globals(module.globals.size());

  size_t entry_func_index = 0;
  bool found = false;
  for (size_t i = 0; i < module.functions.size(); ++i) {
    if (module.functions[i].method_id == module.header.entry_method_id) {
      entry_func_index = i;
      found = true;
      break;
    }
  }
  if (!found) return Trap("entry method not found in functions table");

  std::vector<Value> stack;
  std::vector<Frame> call_stack;

  auto setup_frame = [&](size_t func_index, size_t return_pc, size_t stack_base) -> Frame {
    Frame frame;
    frame.func_index = func_index;
    frame.return_pc = return_pc;
    frame.stack_base = stack_base;
    uint32_t method_id = module.functions[func_index].method_id;
    if (method_id >= module.methods.size()) {
      frame.locals.clear();
      return frame;
    }
    uint16_t local_count = module.methods[method_id].local_count;
    frame.locals.resize(local_count);
    return frame;
  };

  Frame current = setup_frame(entry_func_index, 0, 0);
  size_t pc = module.functions[entry_func_index].code_offset;
  size_t end = pc + module.functions[entry_func_index].code_size;

  while (pc < module.code.size()) {
    if (pc >= end) {
      if (call_stack.empty()) {
        ExecResult done;
        done.status = ExecStatus::Halted;
        return done;
      }
      return Trap("pc out of bounds for function");
    }

    uint8_t opcode = module.code[pc++];
    switch (static_cast<OpCode>(opcode)) {
      case OpCode::Nop:
        break;
      case OpCode::Halt: {
        ExecResult result;
        result.status = ExecStatus::Halted;
        if (!stack.empty() && stack.back().kind == ValueKind::I32) {
          result.exit_code = static_cast<int32_t>(stack.back().i64);
        }
        return result;
      }
      case OpCode::Trap:
        return Trap("TRAP");
      case OpCode::ConstI32: {
        int32_t value = ReadI32(module.code, pc);
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstBool: {
        uint8_t v = ReadU8(module.code, pc);
        Push(stack, Value{ValueKind::Bool, v ? 1 : 0});
        break;
      }
      case OpCode::LoadLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals.size()) return Trap("LOAD_LOCAL out of range");
        Push(stack, current.locals[idx]);
        break;
      }
      case OpCode::StoreLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals.size()) return Trap("STORE_LOCAL out of range");
        current.locals[idx] = Pop(stack);
        break;
      }
      case OpCode::LoadGlobal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= globals.size()) return Trap("LOAD_GLOBAL out of range");
        Push(stack, globals[idx]);
        break;
      }
      case OpCode::StoreGlobal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= globals.size()) return Trap("STORE_GLOBAL out of range");
        globals[idx] = Pop(stack);
        break;
      }
      case OpCode::AddI32:
      case OpCode::SubI32:
      case OpCode::MulI32:
      case OpCode::DivI32: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::I32 || b.kind != ValueKind::I32) return Trap("I32 arithmetic on non-i32");
        int32_t lhs = static_cast<int32_t>(a.i64);
        int32_t rhs = static_cast<int32_t>(b.i64);
        int32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddI32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubI32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulI32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivI32)) out = rhs == 0 ? 0 : (lhs / rhs);
        Push(stack, Value{ValueKind::I32, out});
        break;
      }
      case OpCode::CmpEqI32:
      case OpCode::CmpLtI32: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::I32 || b.kind != ValueKind::I32) return Trap("I32 compare on non-i32");
        int32_t lhs = static_cast<int32_t>(a.i64);
        int32_t rhs = static_cast<int32_t>(b.i64);
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqI32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtI32)) out = (lhs < rhs);
        Push(stack, Value{ValueKind::Bool, out ? 1 : 0});
        break;
      }
      case OpCode::BoolNot: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Bool) return Trap("BOOL_NOT on non-bool");
        Push(stack, Value{ValueKind::Bool, v.i64 ? 0 : 1});
        break;
      }
      case OpCode::Jmp: {
        int32_t rel = ReadI32(module.code, pc);
        pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        break;
      }
      case OpCode::JmpTrue:
      case OpCode::JmpFalse: {
        int32_t rel = ReadI32(module.code, pc);
        Value cond = Pop(stack);
        if (cond.kind != ValueKind::Bool) return Trap("JMP on non-bool");
        bool take = cond.i64 != 0;
        if (opcode == static_cast<uint8_t>(OpCode::JmpFalse)) take = !take;
        if (take) pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        break;
      }
      case OpCode::Enter: {
        uint16_t locals = ReadU16(module.code, pc);
        if (locals != current.locals.size()) return Trap("ENTER local count mismatch");
        break;
      }
      case OpCode::Leave:
        break;
      case OpCode::Call: {
        uint32_t func_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (func_id >= module.functions.size()) return Trap("CALL invalid function id");
        const auto& func = module.functions[func_id];
        if (func.method_id >= module.methods.size()) return Trap("CALL invalid method id");
        const auto& method = module.methods[func.method_id];
        if (method.sig_id >= module.sigs.size()) return Trap("CALL invalid signature id");
        const auto& sig = module.sigs[method.sig_id];
        if (arg_count != sig.param_count) return Trap("CALL arg count mismatch");
        if (stack.size() < arg_count) return Trap("CALL stack underflow");

        std::vector<Value> args(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          args[static_cast<size_t>(i)] = Pop(stack);
        }

        current.return_pc = pc;
        call_stack.push_back(current);
        current = setup_frame(func_id, pc, stack.size());
        for (size_t i = 0; i < args.size() && i < current.locals.size(); ++i) {
          current.locals[i] = args[i];
        }
        pc = func.code_offset;
        end = func.code_offset + func.code_size;
        break;
      }
      case OpCode::Ret: {
        Value ret = {ValueKind::None, 0};
        if (!stack.empty()) ret = Pop(stack);
        if (call_stack.empty()) {
          ExecResult result;
          result.status = ExecStatus::Halted;
          if (ret.kind == ValueKind::I32) result.exit_code = static_cast<int32_t>(ret.i64);
          return result;
        }
        Frame caller = call_stack.back();
        call_stack.pop_back();
        stack.resize(caller.stack_base);
        if (ret.kind != ValueKind::None) Push(stack, ret);
        current = caller;
        pc = current.return_pc;
        const auto& func = module.functions[current.func_index];
        end = func.code_offset + func.code_size;
        break;
      }
      default:
        return Trap("unsupported opcode");
    }
  }

  ExecResult result;
  result.status = ExecStatus::Halted;
  return result;
}

} // namespace simplevm
