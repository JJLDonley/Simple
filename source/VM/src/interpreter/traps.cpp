#include "interpreter/traps.h"

#include <sstream>

#include "opcode.h"

namespace Simple::VM::Interpreter {
namespace {

thread_local TrapContext* g_trap_ctx = nullptr;

} // namespace

TrapContextGuard::TrapContextGuard(TrapContext* ctx) {
  prev = g_trap_ctx;
  g_trap_ctx = ctx;
}

TrapContextGuard::~TrapContextGuard() {
  g_trap_ctx = prev;
}

std::string LookupMethodName(const Simple::Byte::SbcModule& module, size_t func_index) {
  if (func_index >= module.functions.size()) return {};
  const uint32_t method_id = module.functions[func_index].method_id;
  if (method_id >= module.methods.size()) return {};
  const uint32_t name_offset = module.methods[method_id].name_str;
  if (name_offset >= module.const_pool.size()) return {};
  std::string out;
  for (size_t pos = name_offset; pos < module.const_pool.size(); ++pos) {
    const char c = static_cast<char>(module.const_pool[pos]);
    if (c == '\0') break;
    out.push_back(c);
  }
  return out;
}

bool ReadU32Operand(const std::vector<uint8_t>& code, size_t offset, uint32_t& out_val) {
  if (offset + 4 > code.size()) return false;
  out_val = static_cast<uint32_t>(code[offset]) |
            (static_cast<uint32_t>(code[offset + 1]) << 8) |
            (static_cast<uint32_t>(code[offset + 2]) << 16) |
            (static_cast<uint32_t>(code[offset + 3]) << 24);
  return true;
}

bool ReadI32Operand(const std::vector<uint8_t>& code, size_t offset, int32_t& out_val) {
  uint32_t raw = 0;
  if (!ReadU32Operand(code, offset, raw)) return false;
  out_val = static_cast<int32_t>(raw);
  return true;
}

ExecResult Trap(const std::string& message) {
  ExecResult result;
  result.status = ExecStatus::Trapped;
  if (!g_trap_ctx || !g_trap_ctx->current) {
    result.error = message;
    return result;
  }
  std::ostringstream out;
  out << message;
  const FrameState* current = g_trap_ctx->current;
  out << " (func " << current->func_index;
  if (g_trap_ctx->pc >= g_trap_ctx->func_start) {
    out << " pc " << (g_trap_ctx->pc - g_trap_ctx->func_start);
  }
  if (g_trap_ctx->last_opcode != 0xFF) {
    out << " last_op 0x";
    static const char kHex[] = "0123456789ABCDEF";
    out << kHex[(g_trap_ctx->last_opcode >> 4) & 0xF];
    out << kHex[g_trap_ctx->last_opcode & 0xF];
    const char* op_name = Simple::Byte::OpCodeName(g_trap_ctx->last_opcode);
    if (op_name && op_name[0] != '\0') {
      out << " " << op_name;
    }
  }
  if (g_trap_ctx->module && g_trap_ctx->last_opcode != 0xFF) {
    const auto& code = g_trap_ctx->module->code;
    size_t op_pc = g_trap_ctx->pc;
    if (g_trap_ctx->last_opcode == static_cast<uint8_t>(Simple::Byte::OpCode::Call)) {
      uint32_t func_id = 0;
      uint32_t arg_count = 0;
      if (ReadU32Operand(code, op_pc + 1, func_id) && (op_pc + 5) < code.size()) {
        arg_count = code[op_pc + 5];
        out << " operands call func_id=" << func_id << " arg_count=" << arg_count;
      }
    } else if (g_trap_ctx->last_opcode == static_cast<uint8_t>(Simple::Byte::OpCode::Jmp) ||
               g_trap_ctx->last_opcode == static_cast<uint8_t>(Simple::Byte::OpCode::JmpTrue) ||
               g_trap_ctx->last_opcode == static_cast<uint8_t>(Simple::Byte::OpCode::JmpFalse)) {
      int32_t rel = 0;
      if (ReadI32Operand(code, op_pc + 1, rel)) {
        int64_t next_pc = static_cast<int64_t>(op_pc + 1 + 4);
        int64_t target = next_pc + rel;
        out << " operands rel=" << rel;
        if (g_trap_ctx->func_start <= static_cast<size_t>(target)) {
          out << " target_pc=" << (target - static_cast<int64_t>(g_trap_ctx->func_start));
        } else {
          out << " target_pc=" << target;
        }
      }
    } else if (g_trap_ctx->last_opcode == static_cast<uint8_t>(Simple::Byte::OpCode::JmpTable)) {
      uint32_t const_id = 0;
      int32_t def_rel = 0;
      if (ReadU32Operand(code, op_pc + 1, const_id) && ReadI32Operand(code, op_pc + 5, def_rel)) {
        int64_t next_pc = static_cast<int64_t>(op_pc + 1 + 8);
        int64_t target = next_pc + def_rel;
        out << " operands table_const=" << const_id << " default_rel=" << def_rel;
        if (g_trap_ctx->func_start <= static_cast<size_t>(target)) {
          out << " default_target_pc=" << (target - static_cast<int64_t>(g_trap_ctx->func_start));
        } else {
          out << " default_target_pc=" << target;
        }
      }
    }
  }
  if (current->line > 0) {
    out << " line " << current->line;
    if (current->column > 0) out << ":" << current->column;
  }
  std::string name = g_trap_ctx->module ? LookupMethodName(*g_trap_ctx->module, current->func_index) : std::string{};
  if (!name.empty()) {
    out << " name " << name;
  }
  out << ")";
  if (g_trap_ctx->call_stack && !g_trap_ctx->call_stack->empty()) {
    out << " stack:";
    for (auto it = g_trap_ctx->call_stack->rbegin(); it != g_trap_ctx->call_stack->rend(); ++it) {
      out << " <- func " << it->func_index;
      std::string caller_name = g_trap_ctx->module ? LookupMethodName(*g_trap_ctx->module, it->func_index) : std::string{};
      if (!caller_name.empty()) {
        out << " " << caller_name;
      }
      if (it->line > 0) {
        out << " " << it->line;
        if (it->column > 0) out << ":" << it->column;
      }
    }
  }
  result.error = out.str();
  return result;
}

} // namespace Simple::VM::Interpreter
