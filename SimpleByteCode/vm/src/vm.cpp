#include "vm.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

#include "heap.h"
#include "opcode.h"
#include "sbc_verifier.h"

namespace simplevm {
namespace {

using Slot = uint64_t;
constexpr uint32_t kNullRef = 0xFFFFFFFFu;

float BitsToF32(uint32_t bits) {
  uint32_t v = bits;
  float out = 0.0f;
  std::memcpy(&out, &v, sizeof(out));
  return out;
}

double BitsToF64(uint64_t bits) {
  uint64_t v = bits;
  double out = 0.0;
  std::memcpy(&out, &v, sizeof(out));
  return out;
}

uint32_t F32ToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t F64ToBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline Slot PackI32(int32_t value) {
  return static_cast<uint32_t>(value);
}

inline int32_t UnpackI32(Slot value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

inline Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

inline int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
}

inline Slot PackF32Bits(uint32_t bits) {
  return static_cast<uint64_t>(bits);
}

inline Slot PackF64Bits(uint64_t bits) {
  return bits;
}

inline Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

inline uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value);
}

inline bool IsNullRef(Slot value) {
  return UnpackRef(value) == kNullRef;
}

struct Frame {
  size_t func_index = 0;
  size_t return_pc = 0;
  size_t stack_base = 0;
  uint32_t closure_ref = kNullRef;
  uint32_t line = 0;
  uint32_t column = 0;
  std::vector<Slot> locals;
};

struct JitStub {
  bool active = false;
  bool compiled = false;
  bool disabled = false;
};

struct TrapContext {
  Frame* current = nullptr;
  const std::vector<Frame>* call_stack = nullptr;
  const SbcModule* module = nullptr;
  size_t pc = 0;
  size_t func_start = 0;
};

thread_local TrapContext* g_trap_ctx = nullptr;

struct TrapContextGuard {
  TrapContext* prev = nullptr;
  explicit TrapContextGuard(TrapContext* ctx) {
    prev = g_trap_ctx;
    g_trap_ctx = ctx;
  }
  ~TrapContextGuard() {
    g_trap_ctx = prev;
  }
};

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return static_cast<int32_t>(v);
}

int64_t ReadI64(const std::vector<uint8_t>& code, size_t& pc) {
  uint64_t v = static_cast<uint64_t>(code[pc]) |
               (static_cast<uint64_t>(code[pc + 1]) << 8) |
               (static_cast<uint64_t>(code[pc + 2]) << 16) |
               (static_cast<uint64_t>(code[pc + 3]) << 24) |
               (static_cast<uint64_t>(code[pc + 4]) << 32) |
               (static_cast<uint64_t>(code[pc + 5]) << 40) |
               (static_cast<uint64_t>(code[pc + 6]) << 48) |
               (static_cast<uint64_t>(code[pc + 7]) << 56);
  pc += 8;
  return static_cast<int64_t>(v);
}

uint32_t ReadU32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return v;
}

uint64_t ReadU64(const std::vector<uint8_t>& code, size_t& pc) {
  uint64_t v = static_cast<uint64_t>(code[pc]) |
               (static_cast<uint64_t>(code[pc + 1]) << 8) |
               (static_cast<uint64_t>(code[pc + 2]) << 16) |
               (static_cast<uint64_t>(code[pc + 3]) << 24) |
               (static_cast<uint64_t>(code[pc + 4]) << 32) |
               (static_cast<uint64_t>(code[pc + 5]) << 40) |
               (static_cast<uint64_t>(code[pc + 6]) << 48) |
               (static_cast<uint64_t>(code[pc + 7]) << 56);
  pc += 8;
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

Slot Pop(std::vector<Slot>& stack) {
  Slot v = stack.back();
  stack.pop_back();
  return v;
}

void Push(std::vector<Slot>& stack, Slot v) {
  stack.push_back(v);
}

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

uint64_t ReadU64Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint64_t>(payload[offset]) |
         (static_cast<uint64_t>(payload[offset + 1]) << 8) |
         (static_cast<uint64_t>(payload[offset + 2]) << 16) |
         (static_cast<uint64_t>(payload[offset + 3]) << 24) |
         (static_cast<uint64_t>(payload[offset + 4]) << 32) |
         (static_cast<uint64_t>(payload[offset + 5]) << 40) |
         (static_cast<uint64_t>(payload[offset + 6]) << 48) |
         (static_cast<uint64_t>(payload[offset + 7]) << 56);
}

void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint16_t ReadU16Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint16_t>(payload[offset]) |
         (static_cast<uint16_t>(payload[offset + 1]) << 8);
}

void WriteU16Payload(std::vector<uint8_t>& payload, size_t offset, uint16_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint32_t CreateString(Heap& heap, const std::u16string& text) {
  uint32_t length = static_cast<uint32_t>(text.size());
  uint32_t size = 4 + length * 2;
  uint32_t handle = heap.Allocate(ObjectKind::String, 0, size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return 0xFFFFFFFFu;
  WriteU32Payload(obj->payload, 0, length);
  size_t offset = 4;
  for (uint32_t i = 0; i < length; ++i) {
    WriteU16Payload(obj->payload, offset, text[i]);
    offset += 2;
  }
  return handle;
}

std::u16string ReadString(const HeapObject* obj) {
  if (!obj || obj->header.kind != ObjectKind::String) return {};
  uint32_t length = ReadU32Payload(obj->payload, 0);
  std::u16string out;
  out.resize(length);
  size_t offset = 4;
  for (uint32_t i = 0; i < length; ++i) {
    out[i] = static_cast<char16_t>(ReadU16Payload(obj->payload, offset));
    offset += 2;
  }
  return out;
}


ExecResult Trap(const std::string& message) {
  ExecResult result;
  result.status = ExecStatus::Trapped;
  if (!g_trap_ctx || !g_trap_ctx->current) {
    result.error = message;
    return result;
  }
  auto get_method_name = [&](size_t func_index) -> std::string {
    if (!g_trap_ctx->module) return {};
    const auto& module = *g_trap_ctx->module;
    if (func_index >= module.functions.size()) return {};
    uint32_t method_id = module.functions[func_index].method_id;
    if (method_id >= module.methods.size()) return {};
    uint32_t name_offset = module.methods[method_id].name_str;
    if (name_offset >= module.const_pool.size()) return {};
    std::string out;
    for (size_t pos = name_offset; pos < module.const_pool.size(); ++pos) {
      char c = static_cast<char>(module.const_pool[pos]);
      if (c == '\0') break;
      out.push_back(c);
    }
    return out;
  };
  std::ostringstream out;
  out << message;
  const Frame* current = g_trap_ctx->current;
  out << " (func " << current->func_index;
  if (g_trap_ctx->pc >= g_trap_ctx->func_start) {
    out << " pc " << (g_trap_ctx->pc - g_trap_ctx->func_start);
  }
  if (current->line > 0) {
    out << " line " << current->line;
    if (current->column > 0) out << ":" << current->column;
  }
  std::string name = get_method_name(current->func_index);
  if (!name.empty()) {
    out << " name " << name;
  }
  out << ")";
  if (g_trap_ctx->call_stack && !g_trap_ctx->call_stack->empty()) {
    out << " stack:";
    for (auto it = g_trap_ctx->call_stack->rbegin(); it != g_trap_ctx->call_stack->rend(); ++it) {
      out << " <- func " << it->func_index;
      std::string caller_name = get_method_name(it->func_index);
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

} // namespace

ExecResult ExecuteModule(const SbcModule& module) {
  return ExecuteModule(module, true, true);
}

ExecResult ExecuteModule(const SbcModule& module, bool verify) {
  return ExecuteModule(module, verify, true);
}

ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit) {
  VerifyResult vr = VerifyModule(module);
  if (verify && !vr.ok) return Trap(vr.error);
  bool have_meta = vr.ok;
  if (module.functions.empty()) return Trap("no functions to execute");
  if (module.header.entry_method_id == 0xFFFFFFFFu) return Trap("no entry point");

  Heap heap;
  std::vector<Slot> globals(module.globals.size());
  std::vector<uint32_t> call_counts(module.functions.size(), 0);
  std::vector<JitTier> jit_tiers(module.functions.size(), JitTier::None);
  std::vector<JitStub> jit_stubs(module.functions.size());
  std::vector<uint64_t> opcode_counts(256, 0);
  std::vector<uint32_t> compile_counts(module.functions.size(), 0);
  std::vector<uint32_t> func_opcode_counts(module.functions.size(), 0);
  std::vector<uint64_t> compile_ticks_tier0(module.functions.size(), 0);
  std::vector<uint64_t> compile_ticks_tier1(module.functions.size(), 0);
  std::vector<uint32_t> jit_dispatch_counts(module.functions.size(), 0);
  std::vector<uint32_t> jit_compiled_exec_counts(module.functions.size(), 0);
  std::vector<uint32_t> jit_tier1_exec_counts(module.functions.size(), 0);
  uint64_t compile_tick = 0;
  auto can_compile = [&](size_t func_index) -> bool {
    if (func_index >= module.functions.size()) return false;
    const auto& func = module.functions[func_index];
    if (func.method_id >= module.methods.size()) return false;
    const auto& method = module.methods[func.method_id];
    if (method.sig_id >= module.sigs.size()) return false;
    const auto& sig = module.sigs[method.sig_id];
    if (sig.param_count != 0) return false;
    size_t locals_count = 0;
    bool saw_enter = false;
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
        case OpCode::ConstI32: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
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
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          break;
        }
        case OpCode::BoolNot:
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
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
        default:
          return false;
      }
    }
    return true;
  };
  auto run_compiled = [&](size_t func_index, Slot& out_ret, bool& out_has_ret, std::string& error) -> bool {
    if (func_index >= module.functions.size()) {
      error = "JIT compiled invalid function id";
      return false;
    }
    const auto& func = module.functions[func_index];
    size_t pc = func.code_offset;
    size_t end_pc = func.code_offset + func.code_size;
    std::vector<Slot> local_stack;
    std::vector<Slot> locals;
    bool saw_enter = false;
    bool skip_nops = (jit_tiers[func_index] == JitTier::Tier1);
    while (pc < end_pc) {
      uint8_t op = module.code[pc++];
      switch (static_cast<OpCode>(op)) {
        case OpCode::Enter: {
          if (pc + 2 > end_pc) {
            error = "JIT compiled ENTER out of bounds";
            return false;
          }
          uint16_t locals_count = ReadU16(module.code, pc);
          if (!saw_enter) {
            locals.assign(locals_count, 0);
            saw_enter = true;
          } else if (locals.size() != locals_count) {
            error = "JIT compiled locals mismatch";
            return false;
          }
          break;
        }
        case OpCode::Nop:
          if (skip_nops) {
            break;
          }
          break;
        case OpCode::ConstI32: {
          if (pc + 4 > end_pc) {
            error = "JIT compiled CONST_I32 out of bounds";
            return false;
          }
          int32_t value = ReadI32(module.code, pc);
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::AddI32: {
          if (local_stack.size() < 2) {
            error = "JIT compiled ADD_I32 underflow";
            return false;
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
            error = "JIT compiled SUB_I32 underflow";
            return false;
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
            error = "JIT compiled MUL_I32 underflow";
            return false;
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
            error = "JIT compiled DIV_I32 underflow";
            return false;
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          if (b == 0) {
            error = "JIT compiled DIV_I32 by zero";
            return false;
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(a / b)));
          break;
        }
        case OpCode::ModI32: {
          if (local_stack.size() < 2) {
            error = "JIT compiled MOD_I32 underflow";
            return false;
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          if (b == 0) {
            error = "JIT compiled MOD_I32 by zero";
            return false;
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(a % b)));
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          if (local_stack.size() < 2) {
            error = "JIT compiled CMP_I32 underflow";
            return false;
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
        case OpCode::BoolNot: {
          if (local_stack.empty()) {
            error = "JIT compiled BOOL_NOT underflow";
            return false;
          }
          Slot v = local_stack.back();
          local_stack.pop_back();
          local_stack.push_back(PackI32(UnpackI32(v) == 0 ? 1 : 0));
          break;
        }
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          if (local_stack.size() < 2) {
            error = "JIT compiled BOOL binop underflow";
            return false;
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
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          if (pc + 4 > end_pc) {
            error = "JIT compiled JMP out of bounds";
            return false;
          }
          int32_t rel = ReadI32(module.code, pc);
          if (local_stack.empty()) {
            error = "JIT compiled JMP underflow";
            return false;
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
              error = "JIT compiled JMP out of bounds";
              return false;
            }
            pc = static_cast<size_t>(next);
          }
          break;
        }
        case OpCode::Jmp: {
          if (pc + 4 > end_pc) {
            error = "JIT compiled JMP out of bounds";
            return false;
          }
          int32_t rel = ReadI32(module.code, pc);
          int64_t next = static_cast<int64_t>(pc) + rel;
          if (next < static_cast<int64_t>(func.code_offset) || next > static_cast<int64_t>(end_pc)) {
            error = "JIT compiled JMP out of bounds";
            return false;
          }
          pc = static_cast<size_t>(next);
          break;
        }
        case OpCode::LoadLocal: {
          if (pc + 4 > end_pc) {
            error = "JIT compiled LOAD_LOCAL out of bounds";
            return false;
          }
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals.size()) {
            error = "JIT compiled LOAD_LOCAL invalid index";
            return false;
          }
          local_stack.push_back(locals[idx]);
          break;
        }
        case OpCode::StoreLocal: {
          if (pc + 4 > end_pc) {
            error = "JIT compiled STORE_LOCAL out of bounds";
            return false;
          }
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals.size()) {
            error = "JIT compiled STORE_LOCAL invalid index";
            return false;
          }
          if (local_stack.empty()) {
            error = "JIT compiled STORE_LOCAL underflow";
            return false;
          }
          locals[idx] = local_stack.back();
          local_stack.pop_back();
          break;
        }
        case OpCode::Pop: {
          if (local_stack.empty()) {
            error = "JIT compiled POP underflow";
            return false;
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
          error = "JIT compiled unsupported opcode";
          return false;
      }
    }
    error = "JIT compiled missing RET";
    return false;
  };
  auto update_tier = [&](size_t func_index) {
    if (!enable_jit) return;
    if (func_index >= call_counts.size()) return;
    uint32_t count = ++call_counts[func_index];
    if (count >= kJitTier1Threshold) {
      if (jit_tiers[func_index] != JitTier::Tier1) {
        jit_tiers[func_index] = JitTier::Tier1;
        jit_stubs[func_index].active = true;
        jit_stubs[func_index].compiled = jit_stubs[func_index].disabled ? false : can_compile(func_index);
        compile_counts[func_index] += 1;
        compile_ticks_tier1[func_index] = ++compile_tick;
      }
    } else if (count >= kJitTier0Threshold) {
      if (jit_tiers[func_index] == JitTier::None) {
        jit_tiers[func_index] = JitTier::Tier0;
        jit_stubs[func_index].active = true;
        jit_stubs[func_index].compiled = jit_stubs[func_index].disabled ? false : can_compile(func_index);
        compile_counts[func_index] += 1;
        compile_ticks_tier0[func_index] = ++compile_tick;
      }
    }
  };
  auto finish = [&](ExecResult result) {
    result.jit_tiers = jit_tiers;
    result.call_counts = call_counts;
    result.opcode_counts = opcode_counts;
    result.compile_counts = compile_counts;
    result.func_opcode_counts = func_opcode_counts;
    result.compile_ticks_tier0 = compile_ticks_tier0;
    result.compile_ticks_tier1 = compile_ticks_tier1;
    result.jit_dispatch_counts = jit_dispatch_counts;
    result.jit_compiled_exec_counts = jit_compiled_exec_counts;
    result.jit_tier1_exec_counts = jit_tier1_exec_counts;
    return result;
  };
  auto read_const_string = [&](uint32_t const_id, Slot& out_value) -> bool {
    uint32_t kind = ReadU32Payload(module.const_pool, const_id);
    if (kind != 0) return false;
    if (const_id + 8 > module.const_pool.size()) return false;
    uint32_t str_offset = ReadU32Payload(module.const_pool, const_id + 4);
    if (str_offset >= module.const_pool.size()) return false;
    const char* base = reinterpret_cast<const char*>(module.const_pool.data() + str_offset);
    std::u16string text;
    for (size_t i = 0; str_offset + i < module.const_pool.size(); ++i) {
      char c = base[i];
      if (c == '\0') break;
      text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    }
    uint32_t handle = CreateString(heap, text);
    if (handle == 0xFFFFFFFFu) return false;
    out_value = PackRef(handle);
    return true;
  };
  for (size_t i = 0; i < module.globals.size(); ++i) {
    uint32_t const_id = module.globals[i].init_const_id;
    if (const_id == 0xFFFFFFFFu) continue;
    if (const_id + 4 > module.const_pool.size()) return Trap("GLOBAL init const out of bounds");
    uint32_t kind = ReadU32Payload(module.const_pool, const_id);
    if (kind == 0) {
      Slot value = 0;
      if (!read_const_string(const_id, value)) return Trap("GLOBAL init string failed");
      globals[i] = value;
      continue;
    }
    if (kind == 3) {
      if (const_id + 8 > module.const_pool.size()) return Trap("GLOBAL init f32 out of bounds");
      uint32_t bits = ReadU32Payload(module.const_pool, const_id + 4);
      globals[i] = PackF32Bits(bits);
      continue;
    }
    if (kind == 4) {
      if (const_id + 12 > module.const_pool.size()) return Trap("GLOBAL init f64 out of bounds");
      uint64_t bits = ReadU64Payload(module.const_pool, const_id + 4);
      globals[i] = PackF64Bits(bits);
      continue;
    }
    return Trap("GLOBAL init const unsupported");
  }

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

  std::vector<Slot> stack;
  std::vector<Frame> call_stack;

  auto setup_frame = [&](size_t func_index, size_t return_pc, size_t stack_base, uint32_t closure_ref) -> Frame {
    update_tier(func_index);
    Frame frame;
    frame.func_index = func_index;
    frame.return_pc = return_pc;
    frame.stack_base = stack_base;
    frame.closure_ref = closure_ref;
    frame.line = 0;
    frame.column = 0;
    uint32_t method_id = module.functions[func_index].method_id;
    if (method_id >= module.methods.size()) {
      frame.locals.clear();
      return frame;
    }
    uint16_t local_count = module.methods[method_id].local_count;
    frame.locals.resize(local_count);
    return frame;
  };

  size_t func_start = module.functions[entry_func_index].code_offset;
  Frame current = setup_frame(entry_func_index, 0, 0, kNullRef);
  TrapContext trap_ctx;
  trap_ctx.current = &current;
  trap_ctx.call_stack = &call_stack;
  trap_ctx.module = &module;
  trap_ctx.pc = 0;
  trap_ctx.func_start = func_start;
  TrapContextGuard trap_guard(&trap_ctx);
  size_t pc = func_start;
  size_t end = func_start + module.functions[entry_func_index].code_size;

  size_t op_counter = 0;
  auto ref_bit_set = [&](const std::vector<uint8_t>& bits, size_t index) -> bool {
    size_t byte = index / 8;
    if (byte >= bits.size()) return false;
    return (bits[byte] & static_cast<uint8_t>(1u << (index % 8))) != 0;
  };
  auto find_stack_map = [&](size_t func_index, size_t pc_value) -> const StackMap* {
    if (!have_meta || func_index >= vr.methods.size()) return nullptr;
    const auto& maps = vr.methods[func_index].stack_maps;
    for (const auto& map : maps) {
      if (map.pc == pc_value) return &map;
    }
    return nullptr;
  };
  auto maybe_collect = [&]() {
    if (!have_meta) return;
    if (op_counter % 1000 != 0) return;
    const StackMap* stack_map = find_stack_map(current.func_index, pc);
    if (!stack_map) return;
    heap.ResetMarks();
    for (size_t i = 0; i < globals.size(); ++i) {
      if (ref_bit_set(vr.globals_ref_bits, i) && !IsNullRef(globals[i])) {
        heap.Mark(UnpackRef(globals[i]));
      }
    }
    for (size_t i = 0; i < stack_map->stack_height && i < stack.size(); ++i) {
      if (ref_bit_set(stack_map->ref_bits, i) && !IsNullRef(stack[i])) {
        heap.Mark(UnpackRef(stack[i]));
      }
    }
    for (const auto& f : call_stack) {
      if (f.func_index >= vr.methods.size()) continue;
      const auto& bits = vr.methods[f.func_index].locals_ref_bits;
      for (size_t i = 0; i < f.locals.size(); ++i) {
        if (ref_bit_set(bits, i) && !IsNullRef(f.locals[i])) {
          heap.Mark(UnpackRef(f.locals[i]));
        }
      }
    }
    if (current.func_index < vr.methods.size()) {
      const auto& bits = vr.methods[current.func_index].locals_ref_bits;
      for (size_t i = 0; i < current.locals.size(); ++i) {
        if (ref_bit_set(bits, i) && !IsNullRef(current.locals[i])) {
          heap.Mark(UnpackRef(current.locals[i]));
        }
      }
    }
    heap.Sweep();
  };

  while (pc < module.code.size()) {
    trap_ctx.pc = pc;
    trap_ctx.func_start = func_start;
    ++op_counter;
    maybe_collect();
    if (pc >= end) {
      if (call_stack.empty()) {
        ExecResult done;
        done.status = ExecStatus::Halted;
        return finish(done);
      }
      return Trap("pc out of bounds for function");
    }

    uint8_t opcode = module.code[pc++];
    opcode_counts[opcode] += 1;
    if (current.func_index < func_opcode_counts.size()) {
      uint32_t& count = func_opcode_counts[current.func_index];
      count += 1;
      if (enable_jit && count >= kJitOpcodeThreshold && jit_tiers[current.func_index] == JitTier::None) {
        jit_tiers[current.func_index] = JitTier::Tier0;
        jit_stubs[current.func_index].active = true;
        jit_stubs[current.func_index].compiled =
            jit_stubs[current.func_index].disabled ? false : can_compile(current.func_index);
        compile_counts[current.func_index] += 1;
        compile_ticks_tier0[current.func_index] = ++compile_tick;
      }
    }
    switch (static_cast<OpCode>(opcode)) {
      case OpCode::Nop:
        break;
      case OpCode::Halt: {
        ExecResult result;
        result.status = ExecStatus::Halted;
        if (!stack.empty()) {
          result.exit_code = UnpackI32(stack.back());
        }
        return finish(result);
      }
      case OpCode::Trap:
        return Trap("TRAP");
      case OpCode::Breakpoint:
        break;
      case OpCode::Pop: {
        if (stack.empty()) return Trap("POP on empty stack");
        stack.pop_back();
        break;
      }
      case OpCode::Dup: {
        if (stack.empty()) return Trap("DUP on empty stack");
        stack.push_back(stack.back());
        break;
      }
      case OpCode::Dup2: {
        if (stack.size() < 2) return Trap("DUP2 on short stack");
        Slot b = stack[stack.size() - 1];
        Slot a = stack[stack.size() - 2];
        stack.push_back(a);
        stack.push_back(b);
        break;
      }
      case OpCode::Swap: {
        if (stack.size() < 2) return Trap("SWAP on short stack");
        Slot a = stack[stack.size() - 1];
        Slot b = stack[stack.size() - 2];
        stack[stack.size() - 1] = b;
        stack[stack.size() - 2] = a;
        break;
      }
      case OpCode::Rot: {
        if (stack.size() < 3) return Trap("ROT on short stack");
        Slot c = stack[stack.size() - 1];
        Slot b = stack[stack.size() - 2];
        Slot a = stack[stack.size() - 3];
        stack[stack.size() - 3] = b;
        stack[stack.size() - 2] = c;
        stack[stack.size() - 1] = a;
        break;
      }
      case OpCode::ConstI32: {
        int32_t value = ReadI32(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstI64: {
        int64_t value = ReadI64(module.code, pc);
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ConstU32: {
        uint32_t value = ReadU32(module.code, pc);
        Push(stack, PackI32(static_cast<int32_t>(value)));
        break;
      }
      case OpCode::ConstU64: {
        uint64_t value = ReadU64(module.code, pc);
        Push(stack, PackI64(static_cast<int64_t>(value)));
        break;
      }
      case OpCode::ConstI8: {
        int8_t value = static_cast<int8_t>(ReadU8(module.code, pc));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstI16: {
        int16_t value = static_cast<int16_t>(ReadU16(module.code, pc));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstU8: {
        uint8_t value = ReadU8(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstU16: {
        uint16_t value = ReadU16(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstF32: {
        uint32_t bits = ReadU32(module.code, pc);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ConstF64: {
        uint64_t bits = ReadU64(module.code, pc);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ConstI128:
      case OpCode::ConstU128: {
        uint32_t const_id = ReadU32(module.code, pc);
        if (const_id + 8 > module.const_pool.size()) return Trap("CONST_I128/U128 out of bounds");
        uint32_t kind = ReadU32Payload(module.const_pool, const_id);
        uint32_t want = (opcode == static_cast<uint8_t>(OpCode::ConstI128)) ? 1u : 2u;
        if (kind != want) return Trap("CONST_I128/U128 wrong const kind");
        uint32_t blob_offset = ReadU32Payload(module.const_pool, const_id + 4);
        if (blob_offset + 4 > module.const_pool.size()) return Trap("CONST_I128/U128 bad blob offset");
        uint32_t blob_len = ReadU32Payload(module.const_pool, blob_offset);
        if (blob_len < 16) return Trap("CONST_I128/U128 blob too small");
        if (blob_offset + 4 + blob_len > module.const_pool.size()) return Trap("CONST_I128/U128 blob out of bounds");
        Push(stack, PackRef(kNullRef));
        break;
      }
      case OpCode::ConstChar: {
        uint16_t value = ReadU16(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstBool: {
        uint8_t v = ReadU8(module.code, pc);
        Push(stack, PackI32(v ? 1 : 0));
        break;
      }
      case OpCode::ConstString: {
        uint32_t const_id = ReadU32(module.code, pc);
        if (const_id + 8 > module.const_pool.size()) return Trap("CONST_STRING out of bounds");
        uint32_t kind = ReadU32Payload(module.const_pool, const_id);
        if (kind != 0) return Trap("CONST_STRING wrong const kind");
        uint32_t str_offset = ReadU32Payload(module.const_pool, const_id + 4);
        if (str_offset >= module.const_pool.size()) return Trap("CONST_STRING bad offset");
        const char* base = reinterpret_cast<const char*>(module.const_pool.data() + str_offset);
        std::u16string text;
        for (size_t i = 0; str_offset + i < module.const_pool.size(); ++i) {
          char c = base[i];
          if (c == '\0') break;
          text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
        }
        uint32_t handle = CreateString(heap, text);
        if (handle == 0xFFFFFFFFu) return Trap("CONST_STRING allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ConstNull: {
        Push(stack, PackRef(kNullRef));
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
      case OpCode::LoadUpvalue: {
        uint32_t idx = ReadU32(module.code, pc);
        if (current.closure_ref == kNullRef) return Trap("LOAD_UPVALUE without closure");
        HeapObject* obj = heap.Get(current.closure_ref);
        if (!obj || obj->header.kind != ObjectKind::Closure) return Trap("LOAD_UPVALUE on non-closure");
        if (obj->payload.size() < 8) return Trap("LOAD_UPVALUE invalid closure payload");
        uint32_t count = ReadU32Payload(obj->payload, 4);
        if (idx >= count) return Trap("LOAD_UPVALUE out of bounds");
        size_t offset = 8 + static_cast<size_t>(idx) * 4;
        if (offset + 4 > obj->payload.size()) return Trap("LOAD_UPVALUE out of bounds");
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::StoreUpvalue: {
        uint32_t idx = ReadU32(module.code, pc);
        Slot v = Pop(stack);
        if (current.closure_ref == kNullRef) return Trap("STORE_UPVALUE without closure");
        HeapObject* obj = heap.Get(current.closure_ref);
        if (!obj || obj->header.kind != ObjectKind::Closure) return Trap("STORE_UPVALUE on non-closure");
        if (obj->payload.size() < 8) return Trap("STORE_UPVALUE invalid closure payload");
        uint32_t count = ReadU32Payload(obj->payload, 4);
        if (idx >= count) return Trap("STORE_UPVALUE out of bounds");
        size_t offset = 8 + static_cast<size_t>(idx) * 4;
        if (offset + 4 > obj->payload.size()) return Trap("STORE_UPVALUE out of bounds");
        WriteU32Payload(obj->payload, offset, UnpackRef(v));
        break;
      }
      case OpCode::NewObject: {
        uint32_t type_id = ReadU32(module.code, pc);
        if (type_id >= module.types.size()) return Trap("NEW_OBJECT bad type id");
        uint32_t size = module.types[type_id].size;
        uint32_t handle = heap.Allocate(ObjectKind::Artifact, type_id, size);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewClosure: {
        uint32_t method_id = ReadU32(module.code, pc);
        uint8_t upvalue_count = ReadU8(module.code, pc);
        if (method_id >= module.methods.size()) return Trap("NEW_CLOSURE bad method id");
        uint32_t size = 8 + static_cast<uint32_t>(upvalue_count) * 4u;
        uint32_t handle = heap.Allocate(ObjectKind::Closure, method_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_CLOSURE allocation failed");
        WriteU32Payload(obj->payload, 0, method_id);
        WriteU32Payload(obj->payload, 4, static_cast<uint32_t>(upvalue_count));
        if (stack.size() < upvalue_count) return Trap("NEW_CLOSURE stack underflow");
        for (int32_t i = static_cast<int32_t>(upvalue_count) - 1; i >= 0; --i) {
          Slot v = Pop(stack);
          WriteU32Payload(obj->payload, 8 + static_cast<uint32_t>(i) * 4u, UnpackRef(v));
        }
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::LoadField: {
        uint32_t field_id = ReadU32(module.code, pc);
        Slot v = Pop(stack);
        if (field_id >= module.fields.size()) return Trap("LOAD_FIELD bad field id");
        if (IsNullRef(v)) return Trap("LOAD_FIELD on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("LOAD_FIELD on non-object");
        uint32_t offset = module.fields[field_id].offset;
        if (offset + 4 > obj->payload.size()) return Trap("LOAD_FIELD out of bounds");
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::StoreField: {
        uint32_t field_id = ReadU32(module.code, pc);
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (field_id >= module.fields.size()) return Trap("STORE_FIELD bad field id");
        if (IsNullRef(v)) return Trap("STORE_FIELD on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("STORE_FIELD on non-object");
        uint32_t offset = module.fields[field_id].offset;
        if (offset + 4 > obj->payload.size()) return Trap("STORE_FIELD out of bounds");
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        break;
      }
      case OpCode::IsNull: {
        Slot v = Pop(stack);
        Push(stack, PackI32(IsNullRef(v) ? 1 : 0));
        break;
      }
      case OpCode::RefEq:
      case OpCode::RefNe: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        bool out = (UnpackRef(a) == UnpackRef(b));
        if (opcode == static_cast<uint8_t>(OpCode::RefNe)) out = !out;
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::TypeOf: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("TYPEOF on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj) return Trap("TYPEOF on invalid ref");
        Push(stack, PackI32(static_cast<int32_t>(obj->header.type_id)));
        break;
      }
      case OpCode::NewArray: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        uint32_t size = 4 + length * 4;
        uint32_t handle = heap.Allocate(ObjectKind::Array, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_ARRAY allocation failed");
        WriteU32Payload(obj->payload, 0, length);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ArrayLen: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_LEN on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_LEN on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, PackI32(static_cast<int32_t>(length)));
        break;
      }
      case OpCode::ArrayGetI32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ArraySetI32: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        break;
      }
      case OpCode::NewList: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        uint32_t size = 8 + capacity * 4;
        uint32_t handle = heap.Allocate(ObjectKind::List, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_LIST allocation failed");
        WriteU32Payload(obj->payload, 0, 0);
        WriteU32Payload(obj->payload, 4, capacity);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ListLen: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_LEN on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_LEN on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, PackI32(static_cast<int32_t>(length)));
        break;
      }
      case OpCode::ListGetI32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ListSetI32: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        break;
      }
      case OpCode::ListPushI32: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPopI32: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ListInsertI32: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_INSERT overflow");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 4;
          size_t to = 8 + static_cast<size_t>(i) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListRemoveI32: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t removed = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 4;
          size_t to = 8 + static_cast<size_t>(i - 1) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI32(removed));
        break;
      }
      case OpCode::ListClear: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_CLEAR on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_CLEAR on non-list");
        WriteU32Payload(obj->payload, 0, 0);
        break;
      }
      case OpCode::StringLen: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("STRING_LEN on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_LEN on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, PackI32(static_cast<int32_t>(length)));
        break;
      }
      case OpCode::StringConcat: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        if (IsNullRef(a) || IsNullRef(b)) return Trap("STRING_CONCAT on non-ref");
        HeapObject* obj_a = heap.Get(UnpackRef(a));
        HeapObject* obj_b = heap.Get(UnpackRef(b));
        if (!obj_a || !obj_b || obj_a->header.kind != ObjectKind::String || obj_b->header.kind != ObjectKind::String) {
          return Trap("STRING_CONCAT on non-string");
        }
        std::u16string sa = ReadString(obj_a);
        std::u16string sb = ReadString(obj_b);
        std::u16string combined = sa + sb;
        uint32_t handle = CreateString(heap, combined);
        if (handle == 0xFFFFFFFFu) return Trap("STRING_CONCAT allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::StringGetChar: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("STRING_GET_CHAR on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_GET_CHAR on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("STRING_GET_CHAR out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 2;
        uint16_t ch = ReadU16Payload(obj->payload, offset);
        Push(stack, PackI32(ch));
        break;
      }
      case OpCode::StringSlice: {
        Slot end_val = Pop(stack);
        Slot start_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("STRING_SLICE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_SLICE on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t start = UnpackI32(start_val);
        int32_t end_idx = UnpackI32(end_val);
        if (start < 0 || end_idx < 0 || start > end_idx || static_cast<uint32_t>(end_idx) > length) {
          return Trap("STRING_SLICE out of bounds");
        }
        std::u16string text = ReadString(obj);
        std::u16string slice = text.substr(static_cast<size_t>(start), static_cast<size_t>(end_idx - start));
        uint32_t handle = CreateString(heap, slice);
        if (handle == 0xFFFFFFFFu) return Trap("STRING_SLICE allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::CallCheck: {
        if (!call_stack.empty()) return Trap("CALLCHECK not in root");
        break;
      }
      case OpCode::Line: {
        uint32_t line = ReadU32(module.code, pc);
        uint32_t column = ReadU32(module.code, pc);
        current.line = line;
        current.column = column;
        break;
      }
      case OpCode::ProfileStart: {
        ReadU32(module.code, pc);
        break;
      }
      case OpCode::ProfileEnd: {
        ReadU32(module.code, pc);
        break;
      }
      case OpCode::Intrinsic: {
        ReadU32(module.code, pc);
        return Trap("INTRINSIC not supported");
      }
      case OpCode::SysCall: {
        ReadU32(module.code, pc);
        return Trap("SYS_CALL not supported");
      }
      case OpCode::AddI32:
      case OpCode::SubI32:
      case OpCode::MulI32:
      case OpCode::DivI32:
      case OpCode::ModI32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int32_t lhs = UnpackI32(a);
        int32_t rhs = UnpackI32(b);
        int32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddI32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubI32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulI32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivI32)) out = rhs == 0 ? 0 : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModI32)) out = rhs == 0 ? 0 : (lhs % rhs);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::NegI32: {
        Slot a = Pop(stack);
        int32_t out = -UnpackI32(a);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::IncI32:
      case OpCode::DecI32: {
        Slot a = Pop(stack);
        int32_t out = UnpackI32(a);
        if (opcode == static_cast<uint8_t>(OpCode::IncI32)) {
          out += 1;
        } else {
          out -= 1;
        }
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::AddU32:
      case OpCode::SubU32:
      case OpCode::MulU32:
      case OpCode::DivU32:
      case OpCode::ModU32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint32_t lhs = static_cast<uint32_t>(UnpackI32(a));
        uint32_t rhs = static_cast<uint32_t>(UnpackI32(b));
        uint32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddU32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubU32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulU32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivU32)) out = rhs == 0 ? 0u : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModU32)) out = rhs == 0 ? 0u : (lhs % rhs);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::IncU32:
      case OpCode::DecU32: {
        Slot a = Pop(stack);
        uint32_t out = static_cast<uint32_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU32)) {
          out += 1u;
        } else {
          out -= 1u;
        }
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::IncI8:
      case OpCode::DecI8: {
        Slot a = Pop(stack);
        int8_t out = static_cast<int8_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncI8)) {
          out = static_cast<int8_t>(out + 1);
        } else {
          out = static_cast<int8_t>(out - 1);
        }
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::IncI16:
      case OpCode::DecI16: {
        Slot a = Pop(stack);
        int16_t out = static_cast<int16_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncI16)) {
          out = static_cast<int16_t>(out + 1);
        } else {
          out = static_cast<int16_t>(out - 1);
        }
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::IncU8:
      case OpCode::DecU8: {
        Slot a = Pop(stack);
        uint8_t out = static_cast<uint8_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU8)) {
          out = static_cast<uint8_t>(out + 1);
        } else {
          out = static_cast<uint8_t>(out - 1);
        }
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::IncU16:
      case OpCode::DecU16: {
        Slot a = Pop(stack);
        uint16_t out = static_cast<uint16_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU16)) {
          out = static_cast<uint16_t>(out + 1);
        } else {
          out = static_cast<uint16_t>(out - 1);
        }
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::NegI8: {
        Slot a = Pop(stack);
        int8_t v = static_cast<int8_t>(UnpackI32(a));
        int8_t out = static_cast<int8_t>(-v);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::NegI16: {
        Slot a = Pop(stack);
        int16_t v = static_cast<int16_t>(UnpackI32(a));
        int16_t out = static_cast<int16_t>(-v);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::NegU8: {
        Slot a = Pop(stack);
        uint8_t v = static_cast<uint8_t>(UnpackI32(a));
        uint8_t out = static_cast<uint8_t>(0u - v);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::NegU16: {
        Slot a = Pop(stack);
        uint16_t v = static_cast<uint16_t>(UnpackI32(a));
        uint16_t out = static_cast<uint16_t>(0u - v);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::NegU32: {
        Slot a = Pop(stack);
        uint32_t v = static_cast<uint32_t>(UnpackI32(a));
        uint32_t out = 0u - v;
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::AndI32:
      case OpCode::OrI32:
      case OpCode::XorI32:
      case OpCode::ShlI32:
      case OpCode::ShrI32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint32_t lhs = static_cast<uint32_t>(UnpackI32(a));
        uint32_t rhs = static_cast<uint32_t>(UnpackI32(b));
        uint32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AndI32)) out = lhs & rhs;
        if (opcode == static_cast<uint8_t>(OpCode::OrI32)) out = lhs | rhs;
        if (opcode == static_cast<uint8_t>(OpCode::XorI32)) out = lhs ^ rhs;
        if (opcode == static_cast<uint8_t>(OpCode::ShlI32)) out = lhs << (rhs & 31u);
        if (opcode == static_cast<uint8_t>(OpCode::ShrI32)) out = lhs >> (rhs & 31u);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::AddI64:
      case OpCode::SubI64:
      case OpCode::MulI64:
      case OpCode::DivI64:
      case OpCode::ModI64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int64_t lhs = UnpackI64(a);
        int64_t rhs = UnpackI64(b);
        int64_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddI64)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubI64)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulI64)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivI64)) out = rhs == 0 ? 0 : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModI64)) out = rhs == 0 ? 0 : (lhs % rhs);
        Push(stack, PackI64(out));
        break;
      }
      case OpCode::NegI64: {
        Slot a = Pop(stack);
        int64_t out = -UnpackI64(a);
        Push(stack, PackI64(out));
        break;
      }
      case OpCode::NegU64: {
        Slot a = Pop(stack);
        uint64_t v = static_cast<uint64_t>(UnpackI64(a));
        uint64_t out = 0u - v;
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::IncI64:
      case OpCode::DecI64: {
        Slot a = Pop(stack);
        int64_t out = UnpackI64(a);
        if (opcode == static_cast<uint8_t>(OpCode::IncI64)) {
          out += 1;
        } else {
          out -= 1;
        }
        Push(stack, PackI64(out));
        break;
      }
      case OpCode::AddU64:
      case OpCode::SubU64:
      case OpCode::MulU64:
      case OpCode::DivU64:
      case OpCode::ModU64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint64_t lhs = static_cast<uint64_t>(UnpackI64(a));
        uint64_t rhs = static_cast<uint64_t>(UnpackI64(b));
        uint64_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddU64)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubU64)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulU64)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivU64)) out = rhs == 0 ? 0u : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModU64)) out = rhs == 0 ? 0u : (lhs % rhs);
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::IncU64:
      case OpCode::DecU64: {
        Slot a = Pop(stack);
        uint64_t out = static_cast<uint64_t>(UnpackI64(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU64)) {
          out += 1u;
        } else {
          out -= 1u;
        }
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::AndI64:
      case OpCode::OrI64:
      case OpCode::XorI64:
      case OpCode::ShlI64:
      case OpCode::ShrI64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint64_t lhs = static_cast<uint64_t>(UnpackI64(a));
        uint64_t rhs = static_cast<uint64_t>(UnpackI64(b));
        uint64_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AndI64)) out = lhs & rhs;
        if (opcode == static_cast<uint8_t>(OpCode::OrI64)) out = lhs | rhs;
        if (opcode == static_cast<uint8_t>(OpCode::XorI64)) out = lhs ^ rhs;
        if (opcode == static_cast<uint8_t>(OpCode::ShlI64)) out = lhs << (rhs & 63u);
        if (opcode == static_cast<uint8_t>(OpCode::ShrI64)) out = lhs >> (rhs & 63u);
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::AddF32:
      case OpCode::SubF32:
      case OpCode::MulF32:
      case OpCode::DivF32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        float lhs = BitsToF32(static_cast<uint32_t>(a));
        float rhs = BitsToF32(static_cast<uint32_t>(b));
        float out = 0.0f;
        if (opcode == static_cast<uint8_t>(OpCode::AddF32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubF32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulF32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivF32)) out = rhs == 0.0f ? 0.0f : (lhs / rhs);
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::NegF32: {
        Slot a = Pop(stack);
        float out = -BitsToF32(static_cast<uint32_t>(a));
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::IncF32:
      case OpCode::DecF32: {
        Slot a = Pop(stack);
        float out = BitsToF32(static_cast<uint32_t>(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncF32)) {
          out += 1.0f;
        } else {
          out -= 1.0f;
        }
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::AddF64:
      case OpCode::SubF64:
      case OpCode::MulF64:
      case OpCode::DivF64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        double lhs = BitsToF64(static_cast<uint64_t>(a));
        double rhs = BitsToF64(static_cast<uint64_t>(b));
        double out = 0.0;
        if (opcode == static_cast<uint8_t>(OpCode::AddF64)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubF64)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulF64)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivF64)) out = rhs == 0.0 ? 0.0 : (lhs / rhs);
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::NegF64: {
        Slot a = Pop(stack);
        double out = -BitsToF64(static_cast<uint64_t>(a));
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::IncF64:
      case OpCode::DecF64: {
        Slot a = Pop(stack);
        double out = BitsToF64(static_cast<uint64_t>(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncF64)) {
          out += 1.0;
        } else {
          out -= 1.0;
        }
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::CmpEqI32:
      case OpCode::CmpLtI32:
      case OpCode::CmpNeI32:
      case OpCode::CmpLeI32:
      case OpCode::CmpGtI32:
      case OpCode::CmpGeI32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int32_t lhs = UnpackI32(a);
        int32_t rhs = UnpackI32(b);
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqI32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeI32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtI32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeI32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtI32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeI32)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqU32:
      case OpCode::CmpLtU32:
      case OpCode::CmpNeU32:
      case OpCode::CmpLeU32:
      case OpCode::CmpGtU32:
      case OpCode::CmpGeU32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint32_t lhs = static_cast<uint32_t>(UnpackI32(a));
        uint32_t rhs = static_cast<uint32_t>(UnpackI32(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqU32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeU32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtU32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeU32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtU32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeU32)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqI64:
      case OpCode::CmpLtI64:
      case OpCode::CmpNeI64:
      case OpCode::CmpLeI64:
      case OpCode::CmpGtI64:
      case OpCode::CmpGeI64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int64_t lhs = UnpackI64(a);
        int64_t rhs = UnpackI64(b);
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqI64)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeI64)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtI64)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeI64)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtI64)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeI64)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqU64:
      case OpCode::CmpLtU64:
      case OpCode::CmpNeU64:
      case OpCode::CmpLeU64:
      case OpCode::CmpGtU64:
      case OpCode::CmpGeU64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint64_t lhs = static_cast<uint64_t>(UnpackI64(a));
        uint64_t rhs = static_cast<uint64_t>(UnpackI64(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqU64)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeU64)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtU64)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeU64)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtU64)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeU64)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqF32:
      case OpCode::CmpLtF32:
      case OpCode::CmpNeF32:
      case OpCode::CmpLeF32:
      case OpCode::CmpGtF32:
      case OpCode::CmpGeF32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        float lhs = BitsToF32(static_cast<uint32_t>(a));
        float rhs = BitsToF32(static_cast<uint32_t>(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqF32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeF32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtF32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeF32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtF32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeF32)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqF64:
      case OpCode::CmpLtF64:
      case OpCode::CmpNeF64:
      case OpCode::CmpLeF64:
      case OpCode::CmpGtF64:
      case OpCode::CmpGeF64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        double lhs = BitsToF64(static_cast<uint64_t>(a));
        double rhs = BitsToF64(static_cast<uint64_t>(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqF64)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeF64)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtF64)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeF64)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtF64)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeF64)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::BoolNot: {
        Slot v = Pop(stack);
        Push(stack, PackI32(UnpackI32(v) ? 0 : 1));
        break;
      }
      case OpCode::BoolAnd:
      case OpCode::BoolOr: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        bool out = (opcode == static_cast<uint8_t>(OpCode::BoolAnd)) ?
            (UnpackI32(a) != 0 && UnpackI32(b) != 0) :
            (UnpackI32(a) != 0 || UnpackI32(b) != 0);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::Jmp: {
        int32_t rel = ReadI32(module.code, pc);
        pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        if (pc < func_start || pc > end) return Trap("JMP out of bounds");
        break;
      }
      case OpCode::JmpTable: {
        uint32_t const_id = ReadU32(module.code, pc);
        int32_t default_rel = ReadI32(module.code, pc);
        Slot index = Pop(stack);
        if (const_id + 8 > module.const_pool.size()) return Trap("JMP_TABLE const id bad");
        uint32_t kind = ReadU32Payload(module.const_pool, const_id);
        if (kind != 6) return Trap("JMP_TABLE const kind mismatch");
        uint32_t payload = ReadU32Payload(module.const_pool, const_id + 4);
        if (payload + 4 > module.const_pool.size()) return Trap("JMP_TABLE blob out of bounds");
        uint32_t blob_len = ReadU32Payload(module.const_pool, payload);
        if (payload + 4 + blob_len > module.const_pool.size()) return Trap("JMP_TABLE blob out of bounds");
        if (blob_len < 4 || (blob_len - 4) % 4 != 0) return Trap("JMP_TABLE blob size invalid");
        uint32_t count = ReadU32Payload(module.const_pool, payload + 4);
        if (blob_len != 4 + count * 4) return Trap("JMP_TABLE blob size mismatch");
        int32_t rel = default_rel;
        int32_t idx_val = UnpackI32(index);
        if (idx_val >= 0 && static_cast<uint32_t>(idx_val) < count) {
          size_t off_pos = payload + 8 + static_cast<size_t>(idx_val) * 4u;
          uint32_t raw = ReadU32Payload(module.const_pool, off_pos);
          rel = static_cast<int32_t>(raw);
        }
        pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        if (pc < func_start || pc > end) return Trap("JMP_TABLE out of bounds");
        break;
      }
      case OpCode::JmpTrue:
      case OpCode::JmpFalse: {
        int32_t rel = ReadI32(module.code, pc);
        Slot cond = Pop(stack);
        bool take = UnpackI32(cond) != 0;
        if (opcode == static_cast<uint8_t>(OpCode::JmpFalse)) take = !take;
        if (take) {
          pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
          if (pc < func_start || pc > end) return Trap("JMP out of bounds");
        }
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
        if (enable_jit && jit_stubs[func_id].active) {
          // JIT stub placeholder: still runs interpreter path.
          jit_dispatch_counts[func_id] += 1;
        }
        const auto& func = module.functions[func_id];
        if (func.method_id >= module.methods.size()) return Trap("CALL invalid method id");
        const auto& method = module.methods[func.method_id];
        if (method.sig_id >= module.sigs.size()) return Trap("CALL invalid signature id");
        const auto& sig = module.sigs[method.sig_id];
        if (arg_count != sig.param_count) return Trap("CALL arg count mismatch");
        if (stack.size() < arg_count) return Trap("CALL stack underflow");

        std::vector<Slot> args(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          args[static_cast<size_t>(i)] = Pop(stack);
        }

        if (enable_jit && jit_stubs[func_id].compiled) {
          update_tier(func_id);
          jit_compiled_exec_counts[func_id] += 1;
          if (jit_tiers[func_id] == JitTier::Tier1) {
            jit_tier1_exec_counts[func_id] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (run_compiled(func_id, ret, has_ret, error)) {
            if (has_ret) Push(stack, ret);
            break;
          }
          jit_stubs[func_id].compiled = false;
          jit_stubs[func_id].disabled = true;
        }

        current.return_pc = pc;
        current.stack_base = stack.size();
        call_stack.push_back(current);
        current = setup_frame(func_id, pc, stack.size(), kNullRef);
        for (size_t i = 0; i < args.size() && i < current.locals.size(); ++i) {
          current.locals[i] = args[i];
        }
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::CallIndirect: {
        uint32_t sig_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (sig_id >= module.sigs.size()) return Trap("CALL_INDIRECT invalid signature id");
        const auto& sig = module.sigs[sig_id];
        if (arg_count != sig.param_count) return Trap("CALL_INDIRECT arg count mismatch");
        if (stack.size() < static_cast<size_t>(arg_count) + 1u) return Trap("CALL_INDIRECT stack underflow");
        Slot func_val = Pop(stack);
        int64_t func_index = -1;
        uint32_t closure_ref = kNullRef;
        uint32_t handle = UnpackRef(func_val);
        if (handle != kNullRef) {
          HeapObject* obj = heap.Get(handle);
          if (obj && obj->header.kind == ObjectKind::Closure) {
            uint32_t method_id = ReadU32Payload(obj->payload, 0);
            bool found = false;
            for (size_t i = 0; i < module.functions.size(); ++i) {
              if (module.functions[i].method_id == method_id) {
                func_index = static_cast<int64_t>(i);
                found = true;
                break;
              }
            }
            if (!found) return Trap("CALL_INDIRECT closure method not found");
            closure_ref = handle;
          }
        }
        if (func_index < 0) {
          int32_t idx = UnpackI32(func_val);
          if (idx < 0 || static_cast<size_t>(idx) >= module.functions.size()) {
            return Trap("CALL_INDIRECT invalid function id");
          }
          func_index = idx;
        }

        if (enable_jit && jit_stubs[static_cast<size_t>(func_index)].active) {
          // JIT stub placeholder: still runs interpreter path.
          jit_dispatch_counts[static_cast<size_t>(func_index)] += 1;
        }
        std::vector<Slot> args(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          args[static_cast<size_t>(i)] = Pop(stack);
        }

        if (enable_jit && jit_stubs[static_cast<size_t>(func_index)].compiled) {
          update_tier(static_cast<size_t>(func_index));
          jit_compiled_exec_counts[static_cast<size_t>(func_index)] += 1;
          if (jit_tiers[static_cast<size_t>(func_index)] == JitTier::Tier1) {
            jit_tier1_exec_counts[static_cast<size_t>(func_index)] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (run_compiled(static_cast<size_t>(func_index), ret, has_ret, error)) {
            if (has_ret) Push(stack, ret);
            break;
          }
          jit_stubs[static_cast<size_t>(func_index)].compiled = false;
          jit_stubs[static_cast<size_t>(func_index)].disabled = true;
        }

        current.return_pc = pc;
        current.stack_base = stack.size();
        call_stack.push_back(current);
        current = setup_frame(static_cast<size_t>(func_index), pc, stack.size(), closure_ref);
        for (size_t i = 0; i < args.size() && i < current.locals.size(); ++i) {
          current.locals[i] = args[i];
        }
        const auto& func = module.functions[static_cast<size_t>(func_index)];
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::TailCall: {
        uint32_t func_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (func_id >= module.functions.size()) return Trap("TAILCALL invalid function id");
        if (enable_jit && jit_stubs[func_id].active) {
          // JIT stub placeholder: still runs interpreter path.
          jit_dispatch_counts[func_id] += 1;
        }
        const auto& func = module.functions[func_id];
        if (func.method_id >= module.methods.size()) return Trap("TAILCALL invalid method id");
        const auto& method = module.methods[func.method_id];
        if (method.sig_id >= module.sigs.size()) return Trap("TAILCALL invalid signature id");
        const auto& sig = module.sigs[method.sig_id];
        if (arg_count != sig.param_count) return Trap("TAILCALL arg count mismatch");
        if (stack.size() < arg_count) return Trap("TAILCALL stack underflow");

        std::vector<Slot> args(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          args[static_cast<size_t>(i)] = Pop(stack);
        }

        if (enable_jit && jit_stubs[func_id].compiled) {
          update_tier(func_id);
          jit_compiled_exec_counts[func_id] += 1;
          if (jit_tiers[func_id] == JitTier::Tier1) {
            jit_tier1_exec_counts[func_id] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (run_compiled(func_id, ret, has_ret, error)) {
            if (call_stack.empty()) {
              ExecResult result;
              result.status = ExecStatus::Halted;
              if (has_ret) result.exit_code = UnpackI32(ret);
              return finish(result);
            }
            Frame caller = call_stack.back();
            call_stack.pop_back();
            stack.resize(caller.stack_base);
            if (has_ret) Push(stack, ret);
            current = caller;
            pc = current.return_pc;
            const auto& func = module.functions[current.func_index];
            func_start = func.code_offset;
            end = func_start + func.code_size;
            break;
          }
          jit_stubs[func_id].compiled = false;
          jit_stubs[func_id].disabled = true;
        }

        size_t return_pc = current.return_pc;
        size_t stack_base = current.stack_base;
        stack.resize(stack_base);
        current = setup_frame(func_id, return_pc, stack_base, kNullRef);
        for (size_t i = 0; i < args.size() && i < current.locals.size(); ++i) {
          current.locals[i] = args[i];
        }
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::ConvI32ToI64: {
        Slot v = Pop(stack);
        Push(stack, PackI64(static_cast<int64_t>(UnpackI32(v))));
        break;
      }
      case OpCode::ConvI64ToI32: {
        Slot v = Pop(stack);
        Push(stack, PackI32(static_cast<int32_t>(UnpackI64(v))));
        break;
      }
      case OpCode::ConvI32ToF32: {
        Slot v = Pop(stack);
        float out = static_cast<float>(UnpackI32(v));
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::ConvI32ToF64: {
        Slot v = Pop(stack);
        double out = static_cast<double>(UnpackI32(v));
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::ConvF32ToI32: {
        Slot v = Pop(stack);
        float in = BitsToF32(static_cast<uint32_t>(v));
        Push(stack, PackI32(static_cast<int32_t>(in)));
        break;
      }
      case OpCode::ConvF64ToI32: {
        Slot v = Pop(stack);
        double in = BitsToF64(static_cast<uint64_t>(v));
        Push(stack, PackI32(static_cast<int32_t>(in)));
        break;
      }
      case OpCode::ConvF32ToF64: {
        Slot v = Pop(stack);
        double out = static_cast<double>(BitsToF32(static_cast<uint32_t>(v)));
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::ConvF64ToF32: {
        Slot v = Pop(stack);
        float out = static_cast<float>(BitsToF64(static_cast<uint64_t>(v)));
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::Ret: {
        Slot ret = 0;
        bool has_ret = false;
        if (!stack.empty()) {
          ret = Pop(stack);
          has_ret = true;
        }
        if (call_stack.empty()) {
          ExecResult result;
          result.status = ExecStatus::Halted;
          if (has_ret) result.exit_code = UnpackI32(ret);
          return finish(result);
        }
        Frame caller = call_stack.back();
        call_stack.pop_back();
        stack.resize(caller.stack_base);
        if (has_ret) Push(stack, ret);
        current = caller;
        pc = current.return_pc;
        const auto& func = module.functions[current.func_index];
        func_start = func.code_offset;
        end = func_start + func.code_size;
        break;
      }
      default:
        return Trap("unsupported opcode");
    }
  }

  ExecResult result;
  result.status = ExecStatus::Halted;
  return finish(result);
}

} // namespace simplevm
