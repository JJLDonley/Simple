#include "vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ffi/dl_runtime.h"
#include "gc/artifact_trace.h"
#include "gc/root_tracer.h"
#include "gc/stack_map_collection.h"
#include "heap.h"
#include "intrinsic_ids.h"
#include "interpreter/interpreter.h"
#include "interpreter/dispatch.h"
#include "interpreter/frames.h"
#include "interpreter/globals.h"
#include "interpreter/stack.h"
#include "interpreter/traps.h"
#include "jit/llvm_backend.h"
#include "native/buffer.h"
#include "native/channel.h"
#include "native/dispatch.h"
#include "native/env.h"
#include "native/fs.h"
#include "native/json.h"
#include "native/log.h"
#include "native/os.h"
#include "native/path.h"
#include "native/random.h"
#include "native/registry.h"
#include "native/thread.h"
#include "native/time.h"
#include "opcode.h"
#include "scratch_arena.h"
#include "sbc_verifier.h"
#include "runtime/execution_stats.h"
#include "runtime/import_dispatch.h"
#include "runtime/print_any.h"
#include "runtime/runtime_limits.h"
#include "runtime/values.h"

namespace Simple::VM {
namespace {

using Simple::Byte::OpCode;
using Simple::Byte::OpCodeName;
using Simple::Byte::TypeKind;
using Slot = uint64_t;
using Simple::VM::Interpreter::ReadI32;
using Simple::VM::Interpreter::ReadI64;
using Simple::VM::Interpreter::ReadU8;
using Simple::VM::Interpreter::ReadU16;
using Simple::VM::Interpreter::ReadU32;
using Simple::VM::Interpreter::ReadU64;
using Simple::VM::Interpreter::Pop;
using Simple::VM::Interpreter::Push;
using Simple::VM::Interpreter::ReadI32Operand;
using Simple::VM::Interpreter::ReadU32Operand;
using Simple::VM::Interpreter::Trap;
using Simple::VM::Interpreter::TrapContext;
using Simple::VM::Interpreter::TrapContextGuard;
using Simple::VM::Runtime::BitsToF32;
using Simple::VM::Runtime::BitsToF64;
using Simple::VM::Runtime::F32ToBits;
using Simple::VM::Runtime::F64ToBits;
using Simple::VM::Runtime::IsNullRef;
using Simple::VM::Runtime::PackF32;
using Simple::VM::Runtime::PackF32Bits;
using Simple::VM::Runtime::PackF64;
using Simple::VM::Runtime::PackF64Bits;
using Simple::VM::Runtime::PackI32;
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::UnpackI32;
using Simple::VM::Runtime::UnpackF32;
using Simple::VM::Runtime::UnpackF64;
using Simple::VM::Runtime::UnpackI64;
using Simple::VM::Runtime::UnpackRef;
using Simple::VM::Runtime::UnpackU32Bits;
using Simple::VM::Runtime::UnpackU64Bits;
constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

bool CheckedMulOverflowI64(int64_t a, int64_t b, int64_t* out) {
  if (a > 0) {
    if (b > 0) {
      if (a > std::numeric_limits<int64_t>::max() / b) return true;
    } else if (b < 0) {
      if (b < std::numeric_limits<int64_t>::min() / a) return true;
    }
  } else if (a < 0) {
    if (b > 0) {
      if (a < std::numeric_limits<int64_t>::min() / b) return true;
    } else if (b < 0) {
      if (a < std::numeric_limits<int64_t>::max() / b) return true;
    }
  }
  *out = a * b;
  return false;
}

bool CheckedAddOverflowU32(uint32_t a, uint32_t b, uint32_t* out) {
  if (a > std::numeric_limits<uint32_t>::max() - b) return true;
  *out = a + b;
  return false;
}

bool CheckedSubOverflowU32(uint32_t a, uint32_t b, uint32_t* out) {
  if (a < b) return true;
  *out = a - b;
  return false;
}

bool CheckedMulOverflowU32(uint32_t a, uint32_t b, uint32_t* out) {
  if (b != 0 && a > std::numeric_limits<uint32_t>::max() / b) return true;
  *out = a * b;
  return false;
}

bool CheckedAddOverflowU64(uint64_t a, uint64_t b, uint64_t* out) {
  if (a > std::numeric_limits<uint64_t>::max() - b) return true;
  *out = a + b;
  return false;
}

bool CheckedSubOverflowU64(uint64_t a, uint64_t b, uint64_t* out) {
  if (a < b) return true;
  *out = a - b;
  return false;
}

bool CheckedMulOverflowU64(uint64_t a, uint64_t b, uint64_t* out) {
  if (b != 0 && a > std::numeric_limits<uint64_t>::max() / b) return true;
  *out = a * b;
  return false;
}

} // namespace

ExecResult ExecuteModule(const SbcModule& module) {
  return ExecuteModule(module, true, false, ExecOptions{});
}

ExecResult ExecuteModule(const SbcModule& module, bool verify) {
  return ExecuteModule(module, verify, false, ExecOptions{});
}

ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit) {
  return ExecuteModule(module, verify, enable_jit, ExecOptions{});
}

ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit, const ExecOptions& options) {
  if (options.force_interpreter) enable_jit = false;
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(module);
  if (verify && !vr.ok) return Trap(vr.error);
  bool have_meta = vr.ok;
  if (module.functions.empty()) return Trap("no functions to execute");
  if (module.header.entry_method_id == 0xFFFFFFFFu) return Trap("no entry point");
  const RuntimeLimits& limits = options.limits;
  const std::string limit_error = Simple::VM::Runtime::CheckModuleLimits(limits, module);
  if (!limit_error.empty()) return Trap(limit_error);

  Heap heap;
  heap.SetLimits(limits.max_heap_objects, limits.max_heap_bytes);
  if (have_meta) {
    heap.SetArtifactTraceDescriptors(Gc::BuildArtifactTraceDescriptors(module));
  }
  ScratchArena scratch_arena;
  scratch_arena.SetRequireScope(true);
  Simple::VM::Interpreter::InterpreterState interpreter_state =
      Simple::VM::Interpreter::MakeInterpreterState(module.globals.size());
  std::vector<Slot>& globals = interpreter_state.globals;
  std::vector<Slot>& locals_arena = interpreter_state.locals_arena;
  std::vector<Slot> jit_stack;
  std::vector<Slot> jit_locals;
  std::vector<uint32_t> call_counts(module.functions.size(), 0);
  std::vector<JitTier> jit_tiers(module.functions.size(), JitTier::None);
  std::vector<uint64_t> opcode_counts(256, 0);
  std::vector<uint32_t> compile_counts(module.functions.size(), 0);
  std::vector<uint32_t> func_opcode_counts(module.functions.size(), 0);
  std::vector<uint64_t> compile_ticks_tier0(module.functions.size(), 0);
  std::vector<uint64_t> compile_ticks_tier1(module.functions.size(), 0);
  std::vector<uint32_t> jit_dispatch_counts(module.functions.size(), 0);
  std::vector<uint32_t> jit_compiled_exec_counts(module.functions.size(), 0);
  std::vector<uint32_t> jit_tier1_exec_counts(module.functions.size(), 0);
  std::vector<uint32_t> llvm_reject_counts(module.functions.size(), 0);
  std::vector<uint8_t> llvm_rejected(module.functions.size(), 0);
  std::vector<std::string> llvm_reject_reasons(module.functions.size());
  std::vector<Simple::VM::Native::NativeHandleId> file_handles;
  auto promise_registry = std::make_shared<Simple::VM::Runtime::PromiseRegistry>();
  Simple::VM::Native::NativeResourceRegistry resource_registry;
  std::string dl_last_error;
  uint64_t compile_tick = 0;
  Simple::VM::Native::NativeRegistry native_registry = Simple::VM::Native::BuildDefaultRegistry();
  for (size_t i = 0; i < module.globals.size(); ++i) {
    uint32_t const_id = module.globals[i].init_const_id;
    if (const_id == 0xFFFFFFFFu) continue;
    if (const_id + 4 > module.const_pool.size()) return Trap("GLOBAL init const out of bounds");
    uint32_t kind = ReadU32Payload(module.const_pool, const_id);
    if (kind == 0) {
      Slot value = 0;
      if (!Simple::VM::Interpreter::LoadConstStringSlot(module, heap, const_id, value)) return Trap("GLOBAL init string failed");
      globals[i] = value;
      continue;
    }
    if (kind == 3) {
      if (const_id + 8 > module.const_pool.size()) return Trap("GLOBAL init f32 out of bounds");
      uint32_t bits = ReadU32Payload(module.const_pool, const_id + 4);
      if (bits == 0 && Simple::VM::Interpreter::IsRefLikeGlobal(module, i)) {
        globals[i] = PackRef(kNullRef);
        continue;
      }
      globals[i] = PackF32Bits(bits);
      continue;
    }
    if (kind == 4) {
      if (const_id + 12 > module.const_pool.size()) return Trap("GLOBAL init f64 out of bounds");
      uint64_t bits = ReadU64Payload(module.const_pool, const_id + 4);
      if (bits == 0 && Simple::VM::Interpreter::IsRefLikeGlobal(module, i)) {
        globals[i] = PackRef(kNullRef);
        continue;
      }
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

  std::vector<Slot>& stack = interpreter_state.stack;
  std::vector<Simple::VM::Interpreter::FrameState>& call_stack = interpreter_state.call_stack;
  std::vector<Slot>& call_args = interpreter_state.call_args;


  size_t func_start = module.functions[entry_func_index].code_offset;
  call_counts[entry_func_index] += 1;
  Simple::VM::Interpreter::FrameState current = Simple::VM::Interpreter::BuildFrame(module, locals_arena, entry_func_index, 0, 0, kNullRef);
  TrapContext trap_ctx;
  trap_ctx.current = &current;
  trap_ctx.call_stack = &call_stack;
  trap_ctx.module = &module;
  trap_ctx.pc = 0;
  trap_ctx.func_start = func_start;
  TrapContextGuard trap_guard(&trap_ctx);
  size_t pc = func_start;
  size_t end = func_start + module.functions[entry_func_index].code_size;

  auto is_llvm_unsupported = [](const std::string& reason) -> bool {
    return reason.empty() || reason.rfind("unsupported", 0) == 0;
  };
  auto record_llvm_reject = [&](size_t index, const std::string& reason) {
    if (index >= llvm_reject_counts.size()) return;
    llvm_rejected[index] = 1;
    llvm_reject_counts[index] += 1;
    if (!reason.empty()) llvm_reject_reasons[index] = reason;
  };

  if (enable_jit) {
    Simple::VM::Jit::LlvmJitBackend llvm_backend({true, true});
    Slot native_ret = 0;
    bool native_has_ret = false;
    std::string llvm_reason;
    if (llvm_backend.TryRunFunctionWithRuntime(module, entry_func_index, {}, &heap, &globals, &options, native_ret, native_has_ret, llvm_reason)) {
      jit_tiers[entry_func_index] = JitTier::Tier1;
      compile_counts[entry_func_index] += 1;
      compile_ticks_tier1[entry_func_index] = ++compile_tick;
      jit_compiled_exec_counts[entry_func_index] += 1;
      jit_tier1_exec_counts[entry_func_index] += 1;
      ExecResult result;
      result.status = ExecStatus::Halted;
      if (native_has_ret) result.exit_code = UnpackI32(native_ret);
      return Simple::VM::Runtime::AttachExecutionStats(result, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
    }
    record_llvm_reject(entry_func_index, llvm_reason);
    // Entry LLVM is opportunistic during migration. Any rejection or generated
    // IR/runtime issue falls back to the interpreter, which remains semantic
    // authority for diagnostics and traps.
  }

  size_t op_counter = 0;

  while (pc < module.code.size()) {
    trap_ctx.pc = pc;
    trap_ctx.func_start = func_start;
    ++op_counter;
    Simple::VM::Gc::MaybeCollectWithStackMap(have_meta, op_counter, pc, vr, heap, globals, stack, call_stack, current, locals_arena);
    if (pc >= end) {
      if (call_stack.empty()) {
        ExecResult done;
        done.status = ExecStatus::Halted;
        return Simple::VM::Runtime::AttachExecutionStats(done, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
      }
      return Trap("pc out of bounds for function");
    }

    uint8_t opcode = module.code[pc++];
    Simple::Byte::OpVmDispatch vm_dispatch = Simple::Byte::OpVmDispatch::Misc;
    if (!Simple::Byte::GetOpVmDispatch(opcode, &vm_dispatch)) return Trap("unknown opcode");
    trap_ctx.last_opcode = opcode;
    opcode_counts[opcode] += 1;
    if (current.func_index < func_opcode_counts.size()) {
      uint32_t& count = func_opcode_counts[current.func_index];
      count += 1;
      // LLVM JIT migration: opcode hotness is retained for diagnostics only.
      // The old tiered compiled-runner path is intentionally disabled; `-jit`
      // means LLVM ORC attempts with interpreter fallback.
    }
    if (opcode == static_cast<uint8_t>(OpCode::CallNative)) {
      size_t operand_pc = pc;
      if (operand_pc + 7 <= module.code.size()) {
        uint32_t ext_id = ReadU32(module.code, operand_pc);
        uint8_t ext_arg = ReadU8(module.code, operand_pc);
        if (Simple::Byte::IsExtendedOpcodePrefix(opcode, ext_id, ext_arg)) {
          uint16_t ext = ReadU16(module.code, operand_pc);
          pc = operand_pc;
          switch (static_cast<Simple::Byte::ExtendedOpCode>(ext)) {
            case Simple::Byte::ExtendedOpCode::CheckedAddI32: {
              int32_t b = UnpackI32(Pop(stack));
              int32_t a = UnpackI32(Pop(stack));
              int64_t r = static_cast<int64_t>(a) + static_cast<int64_t>(b);
              if (r < std::numeric_limits<int32_t>::min() || r > std::numeric_limits<int32_t>::max()) {
                return Trap("CHECKED_ADD_I32 overflow");
              }
              Push(stack, PackI32(static_cast<int32_t>(r)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedSubI32: {
              int32_t b = UnpackI32(Pop(stack));
              int32_t a = UnpackI32(Pop(stack));
              int64_t r = static_cast<int64_t>(a) - static_cast<int64_t>(b);
              if (r < std::numeric_limits<int32_t>::min() || r > std::numeric_limits<int32_t>::max()) {
                return Trap("CHECKED_SUB_I32 overflow");
              }
              Push(stack, PackI32(static_cast<int32_t>(r)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedMulI32: {
              int32_t b = UnpackI32(Pop(stack));
              int32_t a = UnpackI32(Pop(stack));
              int64_t r = static_cast<int64_t>(a) * static_cast<int64_t>(b);
              if (r < std::numeric_limits<int32_t>::min() || r > std::numeric_limits<int32_t>::max()) {
                return Trap("CHECKED_MUL_I32 overflow");
              }
              Push(stack, PackI32(static_cast<int32_t>(r)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedDivI32: {
              int32_t b = UnpackI32(Pop(stack));
              int32_t a = UnpackI32(Pop(stack));
              if (b == 0) return Trap("CHECKED_DIV_I32 divide by zero");
              if (a == std::numeric_limits<int32_t>::min() && b == -1) {
                return Trap("CHECKED_DIV_I32 overflow");
              }
              Push(stack, PackI32(a / b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedModI32: {
              int32_t b = UnpackI32(Pop(stack));
              int32_t a = UnpackI32(Pop(stack));
              if (b == 0) return Trap("CHECKED_MOD_I32 divide by zero");
              if (a == std::numeric_limits<int32_t>::min() && b == -1) {
                return Trap("CHECKED_MOD_I32 overflow");
              }
              Push(stack, PackI32(a % b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedAddI64: {
              int64_t b = UnpackI64(Pop(stack));
              int64_t a = UnpackI64(Pop(stack));
              if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
                  (b < 0 && a < std::numeric_limits<int64_t>::min() - b)) {
                return Trap("CHECKED_ADD_I64 overflow");
              }
              Push(stack, PackI64(a + b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedSubI64: {
              int64_t b = UnpackI64(Pop(stack));
              int64_t a = UnpackI64(Pop(stack));
              if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
                  (b > 0 && a < std::numeric_limits<int64_t>::min() + b)) {
                return Trap("CHECKED_SUB_I64 overflow");
              }
              Push(stack, PackI64(a - b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedMulI64: {
              int64_t b = UnpackI64(Pop(stack));
              int64_t a = UnpackI64(Pop(stack));
              int64_t r = 0;
              if (CheckedMulOverflowI64(a, b, &r)) return Trap("CHECKED_MUL_I64 overflow");
              Push(stack, PackI64(r));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedDivI64: {
              int64_t b = UnpackI64(Pop(stack));
              int64_t a = UnpackI64(Pop(stack));
              if (b == 0) return Trap("CHECKED_DIV_I64 divide by zero");
              if (a == std::numeric_limits<int64_t>::min() && b == -1) {
                return Trap("CHECKED_DIV_I64 overflow");
              }
              Push(stack, PackI64(a / b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedModI64: {
              int64_t b = UnpackI64(Pop(stack));
              int64_t a = UnpackI64(Pop(stack));
              if (b == 0) return Trap("CHECKED_MOD_I64 divide by zero");
              if (a == std::numeric_limits<int64_t>::min() && b == -1) {
                return Trap("CHECKED_MOD_I64 overflow");
              }
              Push(stack, PackI64(a % b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedAddU32: {
              uint32_t b = UnpackU32Bits(Pop(stack));
              uint32_t a = UnpackU32Bits(Pop(stack));
              uint32_t r = 0;
              if (CheckedAddOverflowU32(a, b, &r)) return Trap("CHECKED_ADD_U32 overflow");
              Push(stack, static_cast<uint64_t>(r));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedSubU32: {
              uint32_t b = UnpackU32Bits(Pop(stack));
              uint32_t a = UnpackU32Bits(Pop(stack));
              uint32_t r = 0;
              if (CheckedSubOverflowU32(a, b, &r)) return Trap("CHECKED_SUB_U32 overflow");
              Push(stack, static_cast<uint64_t>(r));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedMulU32: {
              uint32_t b = UnpackU32Bits(Pop(stack));
              uint32_t a = UnpackU32Bits(Pop(stack));
              uint32_t r = 0;
              if (CheckedMulOverflowU32(a, b, &r)) return Trap("CHECKED_MUL_U32 overflow");
              Push(stack, static_cast<uint64_t>(r));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedDivU32: {
              uint32_t b = UnpackU32Bits(Pop(stack));
              uint32_t a = UnpackU32Bits(Pop(stack));
              if (b == 0) return Trap("CHECKED_DIV_U32 divide by zero");
              Push(stack, static_cast<uint64_t>(a / b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedModU32: {
              uint32_t b = UnpackU32Bits(Pop(stack));
              uint32_t a = UnpackU32Bits(Pop(stack));
              if (b == 0) return Trap("CHECKED_MOD_U32 divide by zero");
              Push(stack, static_cast<uint64_t>(a % b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedAddU64: {
              uint64_t b = UnpackU64Bits(Pop(stack));
              uint64_t a = UnpackU64Bits(Pop(stack));
              uint64_t r = 0;
              if (CheckedAddOverflowU64(a, b, &r)) return Trap("CHECKED_ADD_U64 overflow");
              Push(stack, r);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedSubU64: {
              uint64_t b = UnpackU64Bits(Pop(stack));
              uint64_t a = UnpackU64Bits(Pop(stack));
              uint64_t r = 0;
              if (CheckedSubOverflowU64(a, b, &r)) return Trap("CHECKED_SUB_U64 overflow");
              Push(stack, r);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedMulU64: {
              uint64_t b = UnpackU64Bits(Pop(stack));
              uint64_t a = UnpackU64Bits(Pop(stack));
              uint64_t r = 0;
              if (CheckedMulOverflowU64(a, b, &r)) return Trap("CHECKED_MUL_U64 overflow");
              Push(stack, r);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedDivU64: {
              uint64_t b = UnpackU64Bits(Pop(stack));
              uint64_t a = UnpackU64Bits(Pop(stack));
              if (b == 0) return Trap("CHECKED_DIV_U64 divide by zero");
              Push(stack, a / b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedModU64: {
              uint64_t b = UnpackU64Bits(Pop(stack));
              uint64_t a = UnpackU64Bits(Pop(stack));
              if (b == 0) return Trap("CHECKED_MOD_U64 divide by zero");
              Push(stack, a % b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArrayGetI32: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_GET_I32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_GET_I32 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_GET_I32 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 4;
              Push(stack, PackI32(static_cast<int32_t>(ReadU32Payload(obj->payload, offset))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArraySetI32: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_SET_I32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_SET_I32 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_SET_I32 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 4;
              WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArrayGetI64: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_GET_I64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_GET_I64 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_GET_I64 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_ARRAY_GET_I64 out of bounds");
              Push(stack, PackI64(static_cast<int64_t>(ReadU64Payload(obj->payload, offset))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArraySetI64: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_SET_I64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_SET_I64 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_SET_I64 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_ARRAY_SET_I64 out of bounds");
              WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArrayGetF32: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_GET_F32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_GET_F32 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_GET_F32 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_ARRAY_GET_F32 out of bounds");
              Push(stack, PackF32Bits(ReadU32Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArraySetF32: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_SET_F32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_SET_F32 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_SET_F32 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_ARRAY_SET_F32 out of bounds");
              WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArrayGetF64: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_GET_F64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_GET_F64 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_GET_F64 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_ARRAY_GET_F64 out of bounds");
              Push(stack, PackF64Bits(ReadU64Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArraySetF64: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_SET_F64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_SET_F64 on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_SET_F64 out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_ARRAY_SET_F64 out of bounds");
              WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArrayGetRef: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_GET_REF on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_GET_REF on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_GET_REF out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_ARRAY_GET_REF out of bounds");
              Push(stack, PackRef(ReadU32Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedArraySetRef: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_ARRAY_SET_REF on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Array) return Trap("CHECKED_ARRAY_SET_REF on non-array");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_ARRAY_SET_REF out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_ARRAY_SET_REF out of bounds");
              WriteU32Payload(obj->payload, offset, UnpackRef(value));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListGetI32: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_GET_I32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_GET_I32 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_GET_I32 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_LIST_GET_I32 out of bounds");
              Push(stack, PackI32(static_cast<int32_t>(ReadU32Payload(obj->payload, offset))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListSetI32: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_SET_I32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_SET_I32 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_SET_I32 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_LIST_SET_I32 out of bounds");
              WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListGetI64: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_GET_I64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_GET_I64 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_GET_I64 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_LIST_GET_I64 out of bounds");
              Push(stack, PackI64(static_cast<int64_t>(ReadU64Payload(obj->payload, offset))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListSetI64: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_SET_I64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_SET_I64 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_SET_I64 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_LIST_SET_I64 out of bounds");
              WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListGetF32: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_GET_F32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_GET_F32 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_GET_F32 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_LIST_GET_F32 out of bounds");
              Push(stack, PackF32Bits(ReadU32Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListSetF32: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_SET_F32 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_SET_F32 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_SET_F32 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_LIST_SET_F32 out of bounds");
              WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListGetF64: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_GET_F64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_GET_F64 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_GET_F64 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_LIST_GET_F64 out of bounds");
              Push(stack, PackF64Bits(ReadU64Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListSetF64: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_SET_F64 on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_SET_F64 on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_SET_F64 out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 8;
              if (offset + 8 > obj->payload.size()) return Trap("CHECKED_LIST_SET_F64 out of bounds");
              WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListGetRef: {
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_GET_REF on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_GET_REF on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_GET_REF out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_LIST_GET_REF out of bounds");
              Push(stack, PackRef(ReadU32Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedListSetRef: {
              Slot value = Pop(stack);
              Slot idx = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_LIST_SET_REF on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::List) return Trap("CHECKED_LIST_SET_REF on non-list");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_LIST_SET_REF out of bounds");
              size_t offset = 8 + static_cast<size_t>(index) * 4;
              if (offset + 4 > obj->payload.size()) return Trap("CHECKED_LIST_SET_REF out of bounds");
              WriteU32Payload(obj->payload, offset, UnpackRef(value));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedStringGetChar: {
              Slot idx_val = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_STRING_GET_CHAR on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::String) return Trap("CHECKED_STRING_GET_CHAR on non-string");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t index = UnpackI32(idx_val);
              if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("CHECKED_STRING_GET_CHAR out of bounds");
              size_t offset = 4 + static_cast<size_t>(index) * 2;
              Push(stack, PackI32(ReadU16Payload(obj->payload, offset)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedStringSlice: {
              Slot end_val = Pop(stack);
              Slot start_val = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_STRING_SLICE on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::String) return Trap("CHECKED_STRING_SLICE on non-string");
              uint32_t length = ReadU32Payload(obj->payload, 0);
              int32_t start = UnpackI32(start_val);
              int32_t end_idx = UnpackI32(end_val);
              if (start < 0 || end_idx < 0 || start > end_idx || static_cast<uint32_t>(end_idx) > length) {
                return Trap("CHECKED_STRING_SLICE out of bounds");
              }
              std::u16string text = ReadString(obj);
              uint32_t handle = CreateString(heap, text.substr(static_cast<size_t>(start), static_cast<size_t>(end_idx - start)));
              if (handle == kNullRef) return Trap("CHECKED_STRING_SLICE allocation failed");
              Push(stack, PackRef(handle));
              break;
            }
            case Simple::Byte::ExtendedOpCode::InstanceOf: {
              Slot type_val = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) { Push(stack, PackI32(0)); break; }
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj) return Trap("INSTANCEOF on invalid ref");
              Push(stack, PackI32(obj->header.type_id == static_cast<uint32_t>(UnpackI32(type_val)) ? 1 : 0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CastRef: {
              Slot type_val = Pop(stack);
              (void)type_val;
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CAST_REF on non-ref");
              if (!heap.Get(UnpackRef(v))) return Trap("CAST_REF on invalid ref");
              Push(stack, v);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedCastRef: {
              Slot type_val = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("CHECKED_CAST_REF on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj) return Trap("CHECKED_CAST_REF on invalid ref");
              if (obj->header.type_id != static_cast<uint32_t>(UnpackI32(type_val))) return Trap("CHECKED_CAST_REF type mismatch");
              Push(stack, v);
              break;
            }
            case Simple::Byte::ExtendedOpCode::LoadVTable: {
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("LOAD_VTABLE on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj) return Trap("LOAD_VTABLE on invalid ref");
              Push(stack, PackI32(static_cast<int32_t>(obj->header.type_id)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvI32ToI64: {
              Push(stack, PackI64(static_cast<int64_t>(UnpackI32(Pop(stack)))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvI64ToI32: {
              int64_t in = UnpackI64(Pop(stack));
              if (in < std::numeric_limits<int32_t>::min() || in > std::numeric_limits<int32_t>::max()) {
                return Trap("CHECKED_CONV_I64_I32 overflow");
              }
              Push(stack, PackI32(static_cast<int32_t>(in)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvI32ToF32: {
              Push(stack, PackF32(static_cast<float>(UnpackI32(Pop(stack)))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvI32ToF64: {
              Push(stack, PackF64(static_cast<double>(UnpackI32(Pop(stack)))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvF32ToI32: {
              float in = BitsToF32(static_cast<uint32_t>(Pop(stack)));
              if (!std::isfinite(in) || in < static_cast<float>(std::numeric_limits<int32_t>::min()) ||
                  in > static_cast<float>(std::numeric_limits<int32_t>::max())) {
                return Trap("CHECKED_CONV_F32_I32 overflow");
              }
              Push(stack, PackI32(static_cast<int32_t>(in)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvF64ToI32: {
              double in = BitsToF64(static_cast<uint64_t>(Pop(stack)));
              if (!std::isfinite(in) || in < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
                  in > static_cast<double>(std::numeric_limits<int32_t>::max())) {
                return Trap("CHECKED_CONV_F64_I32 overflow");
              }
              Push(stack, PackI32(static_cast<int32_t>(in)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvF32ToF64: {
              Push(stack, PackF64(static_cast<double>(BitsToF32(static_cast<uint32_t>(Pop(stack))))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::CheckedConvF64ToF32: {
              double in = BitsToF64(static_cast<uint64_t>(Pop(stack)));
              if (std::isfinite(in) && (in < -static_cast<double>(std::numeric_limits<float>::max()) ||
                  in > static_cast<double>(std::numeric_limits<float>::max()))) {
                return Trap("CHECKED_CONV_F64_F32 overflow");
              }
              Push(stack, PackF32(static_cast<float>(in)));
              break;
            }
            case Simple::Byte::ExtendedOpCode::InitObject: {
              int32_t type_raw = UnpackI32(Pop(stack));
              if (type_raw < 0 || static_cast<uint32_t>(type_raw) >= module.types.size()) return Trap("INIT_OBJECT bad type id");
              uint32_t type_id = static_cast<uint32_t>(type_raw);
              uint32_t handle = heap.Allocate(ObjectKind::Artifact, type_id, module.types[type_id].size);
              Push(stack, PackRef(handle));
              break;
            }
            case Simple::Byte::ExtendedOpCode::StringToBytes: {
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("STRING_TO_BYTES on non-ref");
              HeapObject* str = heap.Get(UnpackRef(v));
              if (!str || str->header.kind != ObjectKind::String) return Trap("STRING_TO_BYTES on non-string");
              std::u16string text = ReadString(str);
              uint32_t length = static_cast<uint32_t>(text.size());
              if (!Simple::VM::Runtime::CheckSequenceLimit(limits, length)) return Trap("runtime limit exceeded: string bytes size");
              uint32_t size = 8 + length * 4;
              uint32_t handle = heap.Allocate(ObjectKind::List, 0, size);
              HeapObject* list = heap.Get(handle);
              if (!list) return Trap("STRING_TO_BYTES allocation failed");
              WriteU32Payload(list->payload, 0, length);
              WriteU32Payload(list->payload, 4, length);
              for (uint32_t i = 0; i < length; ++i) {
                WriteU32Payload(list->payload, 8 + static_cast<size_t>(i) * 4, static_cast<uint32_t>(text[i] & 0x00FFu));
              }
              Push(stack, PackRef(handle));
              break;
            }
            case Simple::Byte::ExtendedOpCode::BytesToString: {
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("BYTES_TO_STRING on non-ref");
              HeapObject* list = heap.Get(UnpackRef(v));
              if (!list || list->header.kind != ObjectKind::List) return Trap("BYTES_TO_STRING on non-list");
              uint32_t length = ReadU32Payload(list->payload, 0);
              if (8 + static_cast<size_t>(length) * 4 > list->payload.size()) return Trap("BYTES_TO_STRING out of bounds");
              std::u16string text;
              text.reserve(length);
              for (uint32_t i = 0; i < length; ++i) {
                text.push_back(static_cast<char16_t>(ReadU32Payload(list->payload, 8 + static_cast<size_t>(i) * 4) & 0xFFu));
              }
              uint32_t handle = CreateString(heap, text);
              if (handle == kNullRef) return Trap("BYTES_TO_STRING allocation failed");
              Push(stack, PackRef(handle));
              break;
            }
            case Simple::Byte::ExtendedOpCode::ConstBytes:
            case Simple::Byte::ExtendedOpCode::ConstData:
            case Simple::Byte::ExtendedOpCode::LoadDataRef: {
              int32_t const_raw = UnpackI32(Pop(stack));
              if (const_raw < 0) return Trap("BLOB_CONST bad const id");
              uint32_t const_id = static_cast<uint32_t>(const_raw);
              if (const_id + 8 > module.const_pool.size()) return Trap("BLOB_CONST out of bounds");
              uint32_t kind = ReadU32Payload(module.const_pool, const_id);
              uint32_t expected = static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::ConstBytes ? 7u : 8u;
              if (kind != expected) return Trap("BLOB_CONST kind mismatch");
              uint32_t payload = ReadU32Payload(module.const_pool, const_id + 4);
              if (payload + 4 > module.const_pool.size()) return Trap("BLOB_CONST bad payload");
              uint32_t length = ReadU32Payload(module.const_pool, payload);
              if (payload + 4 + length > module.const_pool.size()) return Trap("BLOB_CONST payload out of bounds");
              if (!Simple::VM::Runtime::CheckSequenceLimit(limits, length)) return Trap("runtime limit exceeded: blob size");
              uint32_t size = 8 + length * 4;
              uint32_t handle = heap.Allocate(ObjectKind::List, 0, size);
              HeapObject* list = heap.Get(handle);
              if (!list) return Trap("BLOB_CONST allocation failed");
              WriteU32Payload(list->payload, 0, length);
              WriteU32Payload(list->payload, 4, length);
              for (uint32_t i = 0; i < length; ++i) {
                WriteU32Payload(list->payload, 8 + static_cast<size_t>(i) * 4, module.const_pool[payload + 4 + i]);
              }
              Push(stack, PackRef(handle));
              break;
            }
            case Simple::Byte::ExtendedOpCode::Throw:
              return Trap("THROW");
            case Simple::Byte::ExtendedOpCode::Panic:
              return Trap("PANIC");
            case Simple::Byte::ExtendedOpCode::CaptureLocal:
            case Simple::Byte::ExtendedOpCode::CaptureRef: {
              int32_t index = UnpackI32(Pop(stack));
              if (index < 0 || static_cast<uint32_t>(index) >= current.locals_count) return Trap("CAPTURE_LOCAL out of range");
              Push(stack, locals_arena[current.locals_base + static_cast<uint32_t>(index)]);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CloseUpvalue: {
              int32_t index = UnpackI32(Pop(stack));
              if (current.closure_ref == kNullRef) return Trap("CLOSE_UPVALUE without closure");
              HeapObject* obj = heap.Get(current.closure_ref);
              if (!obj || obj->header.kind != ObjectKind::Closure) return Trap("CLOSE_UPVALUE on non-closure");
              if (obj->payload.size() < 8) return Trap("CLOSE_UPVALUE invalid closure payload");
              uint32_t count = ReadU32Payload(obj->payload, 4);
              if (index < 0 || static_cast<uint32_t>(index) >= count) return Trap("CLOSE_UPVALUE out of range");
              break;
            }
            case Simple::Byte::ExtendedOpCode::GuardNotNull: {
              Slot value = Pop(stack);
              if (IsNullRef(value)) return Trap("GUARD_NOT_NULL null reference");
              if (!heap.Get(UnpackRef(value))) return Trap("GUARD_NOT_NULL invalid reference");
              Push(stack, value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::GuardBounds: {
              int32_t length = UnpackI32(Pop(stack));
              int32_t index = UnpackI32(Pop(stack));
              Slot value = Pop(stack);
              if (index < 0 || length < 0 || index >= length) return Trap("GUARD_BOUNDS out of bounds");
              Push(stack, value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::GuardType: {
              Slot type_val = Pop(stack);
              Slot v = Pop(stack);
              if (IsNullRef(v)) return Trap("GUARD_TYPE on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj) return Trap("GUARD_TYPE on invalid ref");
              if (obj->header.type_id != static_cast<uint32_t>(UnpackI32(type_val))) return Trap("GUARD_TYPE type mismatch");
              Push(stack, v);
              break;
            }
            case Simple::Byte::ExtendedOpCode::CallMethod: {
              int32_t argc_raw = UnpackI32(Pop(stack));
              int32_t func_raw = UnpackI32(Pop(stack));
              if (argc_raw < 0 || argc_raw > 255 || func_raw < 0) return Trap("CALL_METHOD invalid operands");
              uint8_t arg_count = static_cast<uint8_t>(argc_raw);
              uint32_t func_id = static_cast<uint32_t>(func_raw);
              if (func_id >= module.functions.size()) return Trap("CALL_METHOD invalid function id");
              const auto& func = module.functions[func_id];
              if (func.method_id >= module.methods.size()) return Trap("CALL_METHOD invalid method id");
              const auto& method = module.methods[func.method_id];
              if (method.sig_id >= module.sigs.size()) return Trap("CALL_METHOD invalid signature id");
              const auto& sig = module.sigs[method.sig_id];
              if (arg_count != sig.param_count) return Trap("CALL_METHOD arg count mismatch");
              if (stack.size() < arg_count) return Trap("CALL_METHOD stack underflow");
              call_args.resize(arg_count);
              for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) call_args[static_cast<size_t>(i)] = Pop(stack);
              current.return_pc = pc;
              current.stack_base = stack.size();
              if (!Simple::VM::Runtime::CheckCallDepthLimit(limits, call_stack.size())) return Trap("runtime limit exceeded: call depth");
              call_stack.push_back(current);
              current = Simple::VM::Interpreter::BuildFrame(module, locals_arena, func_id, pc, stack.size(), kNullRef);
              for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) locals_arena[current.locals_base + i] = call_args[i];
              func_start = func.code_offset;
              pc = func_start;
              end = func_start + func.code_size;
              break;
            }
            case Simple::Byte::ExtendedOpCode::CallVirtual: {
              int32_t argc_raw = UnpackI32(Pop(stack));
              int32_t sig_raw = UnpackI32(Pop(stack));
              if (argc_raw < 0 || argc_raw > 255 || sig_raw < 0) return Trap("CALL_VIRTUAL invalid operands");
              uint8_t arg_count = static_cast<uint8_t>(argc_raw);
              uint32_t sig_id = static_cast<uint32_t>(sig_raw);
              if (sig_id >= module.sigs.size()) return Trap("CALL_VIRTUAL invalid signature id");
              const auto& sig = module.sigs[sig_id];
              if (arg_count != sig.param_count) return Trap("CALL_VIRTUAL arg count mismatch");
              if (stack.size() < static_cast<size_t>(arg_count) + 1u) return Trap("CALL_VIRTUAL stack underflow");
              Slot func_val = Pop(stack);
              int64_t func_index = -1;
              uint32_t closure_ref = kNullRef;
              uint32_t handle = UnpackRef(func_val);
              if (handle != kNullRef) {
                HeapObject* obj = heap.Get(handle);
                if (obj && obj->header.kind == ObjectKind::Closure) {
                  uint32_t method_id = ReadU32Payload(obj->payload, 0);
                  for (size_t i = 0; i < module.functions.size(); ++i) {
                    if (module.functions[i].method_id == method_id) { func_index = static_cast<int64_t>(i); break; }
                  }
                  if (func_index < 0) return Trap("CALL_VIRTUAL closure method not found");
                  closure_ref = handle;
                }
              }
              if (func_index < 0) {
                int32_t idx = UnpackI32(func_val);
                if (idx < 0 || static_cast<size_t>(idx) >= module.functions.size()) return Trap("CALL_VIRTUAL invalid function id");
                func_index = idx;
              }
              call_args.resize(arg_count);
              for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) call_args[static_cast<size_t>(i)] = Pop(stack);
              size_t target = static_cast<size_t>(func_index);
              current.return_pc = pc;
              current.stack_base = stack.size();
              if (!Simple::VM::Runtime::CheckCallDepthLimit(limits, call_stack.size())) return Trap("runtime limit exceeded: call depth");
              call_stack.push_back(current);
              current = Simple::VM::Interpreter::BuildFrame(module, locals_arena, target, pc, stack.size(), closure_ref);
              for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) locals_arena[current.locals_base + i] = call_args[i];
              func_start = module.functions[target].code_offset;
              pc = func_start;
              end = func_start + module.functions[target].code_size;
              break;
            }
            case Simple::Byte::ExtendedOpCode::WriteBarrier: {
              Slot value = Pop(stack);
              Slot object = Pop(stack);
              if (IsNullRef(object)) return Trap("WRITE_BARRIER null object");
              if (!heap.Get(UnpackRef(object))) return Trap("WRITE_BARRIER invalid object");
              if (!IsNullRef(value) && !heap.Get(UnpackRef(value))) return Trap("WRITE_BARRIER invalid value");
              break;
            }
            case Simple::Byte::ExtendedOpCode::ReadBarrier: {
              Slot value = Pop(stack);
              if (!IsNullRef(value) && !heap.Get(UnpackRef(value))) return Trap("READ_BARRIER invalid reference");
              Push(stack, value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::PinRef: {
              Slot value = Pop(stack);
              if (IsNullRef(value)) return Trap("PIN_REF null reference");
              if (!heap.Get(UnpackRef(value))) return Trap("PIN_REF invalid reference");
              Push(stack, value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::UnpinRef: {
              Slot value = Pop(stack);
              if (IsNullRef(value)) return Trap("UNPIN_REF null reference");
              if (!heap.Get(UnpackRef(value))) return Trap("UNPIN_REF invalid reference");
              break;
            }
            case Simple::Byte::ExtendedOpCode::LoadPtr: {
              Slot ptr = Pop(stack);
              Push(stack, ptr);
              break;
            }
            case Simple::Byte::ExtendedOpCode::StorePtr: {
              (void)Pop(stack);
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::PtrAdd:
            case Simple::Byte::ExtendedOpCode::PtrOffset: {
              int32_t b = UnpackI32(Pop(stack));
              int32_t a = UnpackI32(Pop(stack));
              Push(stack, PackI32(a + b));
              break;
            }
            case Simple::Byte::ExtendedOpCode::PtrEq:
            case Simple::Byte::ExtendedOpCode::PtrNe: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              bool eq = a == b;
              if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::PtrNe) eq = !eq;
              Push(stack, PackI32(eq ? 1 : 0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::PtrIsNull: {
              Slot ptr = Pop(stack);
              Push(stack, PackI32(IsNullRef(ptr) ? 1 : 0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::PtrCheckNull: {
              Slot ptr = Pop(stack);
              if (IsNullRef(ptr)) return Trap("PTR_CHECK_NULL null pointer");
              Push(stack, ptr);
              break;
            }
            case Simple::Byte::ExtendedOpCode::PtrCheckBounds: {
              int32_t length = UnpackI32(Pop(stack));
              int32_t index = UnpackI32(Pop(stack));
              Slot ptr = Pop(stack);
              if (index < 0 || length < 0 || index >= length) return Trap("PTR_CHECK_BOUNDS out of bounds");
              Push(stack, ptr);
              break;
            }
            case Simple::Byte::ExtendedOpCode::MemCopy:
            case Simple::Byte::ExtendedOpCode::MemMove:
            case Simple::Byte::ExtendedOpCode::MemSet: {
              (void)Pop(stack);
              (void)Pop(stack);
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::MemCompare: {
              (void)Pop(stack);
              (void)Pop(stack);
              (void)Pop(stack);
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::AddressOfLocal: {
              int32_t idx = UnpackI32(Pop(stack));
              if (idx < 0 || static_cast<uint32_t>(idx) >= current.locals_count) return Trap("ADDRESS_OF_LOCAL out of range");
              Push(stack, locals_arena[current.locals_base + static_cast<uint32_t>(idx)]);
              break;
            }
            case Simple::Byte::ExtendedOpCode::AddressOfGlobal: {
              int32_t idx = UnpackI32(Pop(stack));
              if (idx < 0 || static_cast<size_t>(idx) >= globals.size()) return Trap("ADDRESS_OF_GLOBAL out of range");
              Push(stack, globals[static_cast<size_t>(idx)]);
              break;
            }
            case Simple::Byte::ExtendedOpCode::AddressOfField: {
              int32_t field_raw = UnpackI32(Pop(stack));
              Slot v = Pop(stack);
              if (field_raw < 0 || static_cast<uint32_t>(field_raw) >= module.fields.size()) return Trap("ADDRESS_OF_FIELD bad field id");
              if (IsNullRef(v)) return Trap("ADDRESS_OF_FIELD on non-ref");
              HeapObject* obj = heap.Get(UnpackRef(v));
              if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("ADDRESS_OF_FIELD on non-object");
              uint32_t offset = module.fields[static_cast<uint32_t>(field_raw)].offset;
              if (offset + 4 > obj->payload.size()) return Trap("ADDRESS_OF_FIELD out of bounds");
              Push(stack, PackI32(static_cast<int32_t>(ReadU32Payload(obj->payload, offset))));
              break;
            }
            case Simple::Byte::ExtendedOpCode::EnumTag:
            case Simple::Byte::ExtendedOpCode::VariantTag: {
              (void)Pop(stack);
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::EnumPayload:
            case Simple::Byte::ExtendedOpCode::EnumMake:
            case Simple::Byte::ExtendedOpCode::VariantPayload:
            case Simple::Byte::ExtendedOpCode::VariantMake: {
              (void)Pop(stack);
              Slot value = Pop(stack);
              Push(stack, value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::RangeNew: {
              (void)Pop(stack);
              Slot start = Pop(stack);
              Push(stack, start);
              break;
            }
            case Simple::Byte::ExtendedOpCode::RangeNewStep: {
              (void)Pop(stack);
              (void)Pop(stack);
              Slot start = Pop(stack);
              Push(stack, start);
              break;
            }
            case Simple::Byte::ExtendedOpCode::RangeNext:
            case Simple::Byte::ExtendedOpCode::IteratorNext: {
              if (stack.empty()) return Trap("ITERATOR_NEXT stack underflow");
              Push(stack, stack.back());
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::IteratorHasNext: {
              (void)Pop(stack);
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::IteratorValue: {
              Slot iter = Pop(stack);
              Push(stack, iter);
              break;
            }
            case Simple::Byte::ExtendedOpCode::Spawn:
            case Simple::Byte::ExtendedOpCode::MakeFuture: {
              Slot func = Pop(stack);
              int32_t id = UnpackI32(func);
              if (id < 0 || static_cast<uint32_t>(id) >= module.functions.size()) return Trap("TASK_CREATE invalid function id");
              Push(stack, func);
              break;
            }
            case Simple::Byte::ExtendedOpCode::Join:
            case Simple::Byte::ExtendedOpCode::Await:
            case Simple::Byte::ExtendedOpCode::PollFuture: {
              Slot handle = Pop(stack);
              Push(stack, handle);
              break;
            }
            case Simple::Byte::ExtendedOpCode::Detach:
            case Simple::Byte::ExtendedOpCode::Resume: {
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::Suspend: {
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::ChannelSend: {
              (void)Pop(stack);
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::ChannelRecv: {
              (void)Pop(stack);
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::ChannelTryRecv: {
              (void)Pop(stack);
              Push(stack, PackI32(0));
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::AtomicLoad: {
              Slot address = Pop(stack);
              Push(stack, address);
              break;
            }
            case Simple::Byte::ExtendedOpCode::AtomicStore: {
              (void)Pop(stack);
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::AtomicAdd: {
              Slot value = Pop(stack);
              Slot address = Pop(stack);
              Push(stack, address + value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::AtomicSub: {
              Slot value = Pop(stack);
              Slot address = Pop(stack);
              Push(stack, address - value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::AtomicCompareExchange: {
              (void)Pop(stack);
              (void)Pop(stack);
              (void)Pop(stack);
              Push(stack, PackI32(0));
              break;
            }
            case Simple::Byte::ExtendedOpCode::Lock:
            case Simple::Byte::ExtendedOpCode::Unlock:
            case Simple::Byte::ExtendedOpCode::Wait:
            case Simple::Byte::ExtendedOpCode::Notify:
            case Simple::Byte::ExtendedOpCode::NotifyAll: {
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::TryLock: {
              (void)Pop(stack);
              Push(stack, PackI32(1));
              break;
            }
            case Simple::Byte::ExtendedOpCode::SourceSpan: {
              Slot line = Pop(stack);
              current.line = static_cast<uint32_t>(UnpackI32(line));
              break;
            }
            case Simple::Byte::ExtendedOpCode::Catch:
            case Simple::Byte::ExtendedOpCode::Finally:
            case Simple::Byte::ExtendedOpCode::Deopt:
            case Simple::Byte::ExtendedOpCode::Patchpoint:
            case Simple::Byte::ExtendedOpCode::InlineCache: {
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecLoad:
            case Simple::Byte::ExtendedOpCode::VecSplat: {
              Slot value = Pop(stack);
              Push(stack, value);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecStore: {
              (void)Pop(stack);
              (void)Pop(stack);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecExtract: {
              (void)Pop(stack);
              Slot vector = Pop(stack);
              Push(stack, vector);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecAdd: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              Push(stack, a + b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecSub: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              Push(stack, a - b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecMul: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              Push(stack, a * b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecDiv: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              if (b == 0) return Trap("VEC_DIV divide by zero");
              Push(stack, a / b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecAnd: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              Push(stack, a & b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecOr: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              Push(stack, a | b);
              break;
            }
            case Simple::Byte::ExtendedOpCode::VecXor: {
              Slot b = Pop(stack);
              Slot a = Pop(stack);
              Push(stack, a ^ b);
              break;
            }
            default:
              return Trap("unknown extended opcode");
          }
          continue;
        }
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
        return Simple::VM::Runtime::AttachExecutionStats(result, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
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
        if (handle == kNullRef) return Trap("CONST_STRING allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ConstNull: {
        Push(stack, PackRef(kNullRef));
        break;
      }
      case OpCode::LoadLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals_count) return Trap("LOAD_LOCAL out of range");
        Push(stack, locals_arena[current.locals_base + idx]);
        break;
      }
      case OpCode::StoreLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals_count) return Trap("STORE_LOCAL out of range");
        locals_arena[current.locals_base + idx] = Pop(stack);
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
      case OpCode::InitGlobal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= globals.size()) return Trap("INIT_GLOBAL out of range");
        break;
      }
      case OpCode::InitModule:
      case OpCode::EnsureModuleInit:
        ReadU32(module.code, pc);
        break;
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
        const auto& field = module.fields[field_id];
        if (field.type_id >= module.types.size()) return Trap("LOAD_FIELD bad field type");
        uint32_t offset = field.offset;
        const uint32_t width = module.types[field.type_id].size;
        if (width == 8) {
          if (offset + 8 > obj->payload.size()) return Trap("LOAD_FIELD out of bounds");
          Push(stack, ReadU64Payload(obj->payload, offset));
        } else {
          if (width == 0 || width > 4 || offset + 4 > obj->payload.size()) {
            return Trap("LOAD_FIELD unsupported field width");
          }
          Push(stack, static_cast<uint64_t>(ReadU32Payload(obj->payload, offset)));
        }
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
        const auto& field = module.fields[field_id];
        if (field.type_id >= module.types.size()) return Trap("STORE_FIELD bad field type");
        uint32_t offset = field.offset;
        const uint32_t width = module.types[field.type_id].size;
        if (width == 8) {
          if (offset + 8 > obj->payload.size()) return Trap("STORE_FIELD out of bounds");
          WriteU64Payload(obj->payload, offset, value);
        } else {
          if (width == 0 || width > 4 || offset + 4 > obj->payload.size()) {
            return Trap("STORE_FIELD unsupported field width");
          }
          WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(value));
        }
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
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, length)) return Trap("runtime limit exceeded: array/list size");
        uint32_t size = 4 + length * 4;
        uint32_t handle = heap.Allocate(ObjectKind::Array, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_ARRAY allocation failed");
        WriteU32Payload(obj->payload, 0, length);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewArrayI64:
      case OpCode::NewArrayF64: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, length)) return Trap("runtime limit exceeded: array/list size");
        uint32_t size = 4 + length * 8;
        uint32_t handle = heap.Allocate(ObjectKind::Array, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_ARRAY allocation failed");
        WriteU32Payload(obj->payload, 0, length);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewArrayF32:
      case OpCode::NewArrayRef: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, length)) return Trap("runtime limit exceeded: array/list size");
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
      case OpCode::ArrayGetI64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        int64_t value = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ArrayGetF32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        uint32_t bits = ReadU32Payload(obj->payload, offset);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ArrayGetF64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        uint64_t bits = ReadU64Payload(obj->payload, offset);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ArrayGetRef: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        Push(stack, PackRef(handle));
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
      case OpCode::ArraySetI64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        break;
      }
      case OpCode::ArraySetF32: {
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
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        break;
      }
      case OpCode::ArraySetF64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        break;
      }
      case OpCode::ArraySetRef: {
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
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
        break;
      }
      case OpCode::ArrayCopy: {
        int32_t count = UnpackI32(Pop(stack));
        int32_t dst_index = UnpackI32(Pop(stack));
        Slot dst_value = Pop(stack);
        int32_t src_index = UnpackI32(Pop(stack));
        Slot src_value = Pop(stack);
        if (IsNullRef(src_value) || IsNullRef(dst_value)) return Trap("ARRAY_COPY on non-ref");
        HeapObject* src = heap.Get(UnpackRef(src_value));
        HeapObject* dst = heap.Get(UnpackRef(dst_value));
        if (!src || !dst || src->header.kind != ObjectKind::Array || dst->header.kind != ObjectKind::Array) {
          return Trap("ARRAY_COPY on non-array");
        }
        if (src->header.type_id != dst->header.type_id) return Trap("ARRAY_COPY type mismatch");
        uint32_t src_len = ReadU32Payload(src->payload, 0);
        uint32_t dst_len = ReadU32Payload(dst->payload, 0);
        if (count < 0 || src_index < 0 || dst_index < 0 ||
            static_cast<uint32_t>(src_index) + static_cast<uint32_t>(count) > src_len ||
            static_cast<uint32_t>(dst_index) + static_cast<uint32_t>(count) > dst_len) {
          return Trap("ARRAY_COPY out of bounds");
        }
        uint32_t elem_size = 4;
        if (src->header.type_id < module.types.size()) {
          TypeKind kind = static_cast<TypeKind>(module.types[src->header.type_id].kind);
          if (kind == TypeKind::I64 || kind == TypeKind::U64 || kind == TypeKind::F64) elem_size = 8;
        }
        size_t bytes = static_cast<size_t>(count) * elem_size;
        size_t src_offset = HeapLayout::ArrayElementOffset(static_cast<uint32_t>(src_index), elem_size);
        size_t dst_offset = HeapLayout::ArrayElementOffset(static_cast<uint32_t>(dst_index), elem_size);
        if (src_offset + bytes > src->payload.size() || dst_offset + bytes > dst->payload.size()) {
          return Trap("ARRAY_COPY invalid payload");
        }
        std::memmove(dst->payload.data() + dst_offset, src->payload.data() + src_offset, bytes);
        break;
      }
      case OpCode::ArrayFill: {
        Slot fill = Pop(stack);
        int32_t count = UnpackI32(Pop(stack));
        Slot array_value = Pop(stack);
        if (IsNullRef(array_value)) return Trap("ARRAY_FILL on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(array_value));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_FILL on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (count < 0 || static_cast<uint32_t>(count) > length) return Trap("ARRAY_FILL out of bounds");
        uint32_t elem_size = 4;
        if (obj->header.type_id < module.types.size()) {
          TypeKind kind = static_cast<TypeKind>(module.types[obj->header.type_id].kind);
          if (kind == TypeKind::I64 || kind == TypeKind::U64 || kind == TypeKind::F64) elem_size = 8;
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
          size_t offset = HeapLayout::ArrayElementOffset(i, elem_size);
          if (elem_size == 8) WriteU64Payload(obj->payload, offset, fill);
          else WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(fill));
        }
        break;
      }
      case OpCode::NewList: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, capacity)) return Trap("runtime limit exceeded: array/list size");
        uint32_t size = 8 + capacity * 4;
        uint32_t handle = heap.Allocate(ObjectKind::List, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_LIST allocation failed");
        WriteU32Payload(obj->payload, 0, 0);
        WriteU32Payload(obj->payload, 4, capacity);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewListI64:
      case OpCode::NewListF64: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, capacity)) return Trap("runtime limit exceeded: array/list size");
        uint32_t size = 8 + capacity * 8;
        uint32_t handle = heap.Allocate(ObjectKind::List, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_LIST allocation failed");
        WriteU32Payload(obj->payload, 0, 0);
        WriteU32Payload(obj->payload, 4, capacity);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewListF32:
      case OpCode::NewListRef: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, capacity)) return Trap("runtime limit exceeded: array/list size");
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
      case OpCode::ListGetI64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        int64_t value = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ListGetF32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t bits = ReadU32Payload(obj->payload, offset);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ListGetF64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        uint64_t bits = ReadU64Payload(obj->payload, offset);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ListGetRef: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        Push(stack, PackRef(handle));
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
      case OpCode::ListSetI64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        break;
      }
      case OpCode::ListSetF32: {
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
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        break;
      }
      case OpCode::ListSetF64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        break;
      }
      case OpCode::ListSetRef: {
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
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
        break;
      }
      case OpCode::ListPushI32: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 4)) return Trap("LIST_PUSH invalid list");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushI64: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 8)) return Trap("LIST_PUSH invalid list");
        size_t offset = 8 + static_cast<size_t>(length) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushF32: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 4)) return Trap("LIST_PUSH invalid list");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushF64: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 8)) return Trap("LIST_PUSH invalid list");
        size_t offset = 8 + static_cast<size_t>(length) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushRef: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 4)) return Trap("LIST_PUSH invalid list");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
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
      case OpCode::ListPopI64: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        int64_t value = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ListPopF32: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t bits = ReadU32Payload(obj->payload, offset);
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ListPopF64: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        uint64_t bits = ReadU64Payload(obj->payload, offset);
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ListPopRef: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackRef(handle));
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
        if (!EnsureListCapacity(obj, length + 1, 4)) return Trap("LIST_INSERT invalid list");
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
      case OpCode::ListInsertI64: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 8)) return Trap("LIST_INSERT invalid list");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 8;
          size_t to = 8 + static_cast<size_t>(i) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertF32: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 4)) return Trap("LIST_INSERT invalid list");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 4;
          size_t to = 8 + static_cast<size_t>(i) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertF64: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 8)) return Trap("LIST_INSERT invalid list");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 8;
          size_t to = 8 + static_cast<size_t>(i) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertRef: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, length + 1, 4)) return Trap("LIST_INSERT invalid list");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 4;
          size_t to = 8 + static_cast<size_t>(i) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
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
      case OpCode::ListRemoveI64: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        int64_t removed = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 8;
          size_t to = 8 + static_cast<size_t>(i - 1) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI64(removed));
        break;
      }
      case OpCode::ListRemoveF32: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t removed = ReadU32Payload(obj->payload, offset);
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 4;
          size_t to = 8 + static_cast<size_t>(i - 1) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF32Bits(removed));
        break;
      }
      case OpCode::ListRemoveF64: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        uint64_t removed = ReadU64Payload(obj->payload, offset);
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 8;
          size_t to = 8 + static_cast<size_t>(i - 1) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF64Bits(removed));
        break;
      }
      case OpCode::ListRemoveRef: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t removed = ReadU32Payload(obj->payload, offset);
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 4;
          size_t to = 8 + static_cast<size_t>(i - 1) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackRef(removed));
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
      case OpCode::ListReserve: {
        Slot capacity_value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_RESERVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_RESERVE on non-list");
        int32_t requested = UnpackI32(capacity_value);
        if (requested < 0) return Trap("LIST_RESERVE negative capacity");
        uint32_t capacity = static_cast<uint32_t>(requested);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, capacity)) return Trap("runtime limit exceeded: array/list size");
        uint32_t elem_size = 4;
        if (obj->header.type_id < module.types.size()) {
          TypeKind kind = static_cast<TypeKind>(module.types[obj->header.type_id].kind);
          if (kind == TypeKind::I64 || kind == TypeKind::U64 || kind == TypeKind::F64) elem_size = 8;
        }
        if (!EnsureListCapacity(obj, capacity, elem_size)) return Trap("LIST_RESERVE invalid list");
        break;
      }
      case OpCode::ListResize: {
        Slot fill = Pop(stack);
        Slot size_value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_RESIZE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_RESIZE on non-list");
        int32_t requested = UnpackI32(size_value);
        if (requested < 0) return Trap("LIST_RESIZE negative size");
        uint32_t new_length = static_cast<uint32_t>(requested);
        if (!Simple::VM::Runtime::CheckSequenceLimit(limits, new_length)) return Trap("runtime limit exceeded: array/list size");
        uint32_t elem_size = 4;
        if (obj->header.type_id < module.types.size()) {
          TypeKind kind = static_cast<TypeKind>(module.types[obj->header.type_id].kind);
          if (kind == TypeKind::I64 || kind == TypeKind::U64 || kind == TypeKind::F64) elem_size = 8;
        }
        uint32_t old_length = ReadU32Payload(obj->payload, 0);
        if (!EnsureListCapacity(obj, new_length, elem_size)) return Trap("LIST_RESIZE invalid list");
        for (uint32_t i = old_length; i < new_length; ++i) {
          size_t offset = HeapLayout::ListElementOffset(i, elem_size);
          if (elem_size == 8) WriteU64Payload(obj->payload, offset, fill);
          else WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(fill));
        }
        WriteU32Payload(obj->payload, 0, new_length);
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
        if (handle == kNullRef) return Trap("STRING_CONCAT allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::StringEq:
      case OpCode::StringNe:
      case OpCode::StringCompare:
      case OpCode::StringFind: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        if (IsNullRef(a) || IsNullRef(b)) return Trap("STRING_COMPARE on non-ref");
        HeapObject* obj_a = heap.Get(UnpackRef(a));
        HeapObject* obj_b = heap.Get(UnpackRef(b));
        if (!obj_a || !obj_b || obj_a->header.kind != ObjectKind::String || obj_b->header.kind != ObjectKind::String) {
          return Trap("STRING_COMPARE on non-string");
        }
        std::u16string sa = ReadString(obj_a);
        std::u16string sb = ReadString(obj_b);
        if (opcode == static_cast<uint8_t>(OpCode::StringCompare)) {
          const int cmp = sa < sb ? -1 : (sa > sb ? 1 : 0);
          Push(stack, PackI32(cmp));
        } else if (opcode == static_cast<uint8_t>(OpCode::StringFind)) {
          const size_t pos = sa.find(sb);
          Push(stack, PackI32(pos == std::u16string::npos ? -1 : static_cast<int32_t>(pos)));
        } else {
          bool out = sa == sb;
          if (opcode == static_cast<uint8_t>(OpCode::StringNe)) out = !out;
          Push(stack, PackI32(out ? 1 : 0));
        }
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
        if (handle == kNullRef) return Trap("STRING_SLICE allocation failed");
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
      case OpCode::Safepoint:
      case OpCode::AllocCheckpoint:
      case OpCode::ExitSandbox:
        break;
      case OpCode::Yield:
        Simple::VM::Native::Thread::Yield();
        break;
      case OpCode::Fence:
        std::atomic_thread_fence(std::memory_order_seq_cst);
        break;
      case OpCode::CheckCapability:
      case OpCode::EnterSandbox:
        ReadU32(module.code, pc);
        break;
      case OpCode::KeepAlive:
        Pop(stack);
        break;
      case OpCode::TraceEnter:
      case OpCode::TraceLeave: {
        ReadU32(module.code, pc);
        break;
      }
      case OpCode::StackTrace: {
        std::ostringstream trace;
        trace << "func#" << current.func_index << ":" << current.line << ":" << current.column;
        for (auto it = call_stack.rbegin(); it != call_stack.rend(); ++it) {
          trace << "\nfunc#" << it->func_index << ":" << it->line << ":" << it->column;
        }
        uint32_t handle = CreateString(heap, AsciiToU16(trace.str()));
        if (handle == kNullRef) return Trap("STACKTRACE allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::CheckedNull: {
        Slot value = Pop(stack);
        if (IsNullRef(value)) return Trap("CHECKED_NULL null reference");
        if (!heap.Get(UnpackRef(value))) return Trap("CHECKED_NULL invalid reference");
        Push(stack, value);
        break;
      }
      case OpCode::CheckedBounds: {
        int32_t length = UnpackI32(Pop(stack));
        int32_t index = UnpackI32(Pop(stack));
        Slot value = Pop(stack);
        if (index < 0 || length < 0 || index >= length) return Trap("CHECKED_BOUNDS out of bounds");
        Push(stack, value);
        break;
      }
      case OpCode::DropObject: {
        Slot value = Pop(stack);
        if (IsNullRef(value)) return Trap("DROP_OBJECT null reference");
        if (!heap.Get(UnpackRef(value))) return Trap("DROP_OBJECT invalid reference");
        break;
      }
      case OpCode::CloneObject: {
        Slot value = Pop(stack);
        if (IsNullRef(value)) return Trap("CLONE_OBJECT null reference");
        HeapObject* obj = heap.Get(UnpackRef(value));
        if (!obj) return Trap("CLONE_OBJECT invalid reference");
        const ObjectKind kind = obj->header.kind;
        const uint32_t type_id = obj->header.type_id;
        const uint32_t size = obj->header.size;
        const std::vector<uint8_t> payload = obj->payload;
        uint32_t handle = heap.Allocate(kind, type_id, size);
        if (handle == kNullRef) return Trap("CLONE_OBJECT allocation failed");
        HeapObject* clone = heap.Get(handle);
        if (!clone) return Trap("CLONE_OBJECT allocation invalid");
        clone->payload = payload;
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ObjectEq: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        if (IsNullRef(a) || IsNullRef(b)) return Trap("OBJECT_EQ null reference");
        HeapObject* obj_a = heap.Get(UnpackRef(a));
        HeapObject* obj_b = heap.Get(UnpackRef(b));
        if (!obj_a || !obj_b) return Trap("OBJECT_EQ invalid reference");
        bool equal = false;
        if (obj_a->header.kind == ObjectKind::String && obj_b->header.kind == ObjectKind::String) {
          equal = ReadString(obj_a) == ReadString(obj_b);
        } else {
          equal = obj_a->header.kind == obj_b->header.kind &&
                  obj_a->header.type_id == obj_b->header.type_id &&
                  obj_a->payload == obj_b->payload;
        }
        Push(stack, PackI32(equal ? 1 : 0));
        break;
      }
      case OpCode::Intrinsic: {
        uint32_t id = ReadU32(module.code, pc);
        switch (id) {
          case kIntrinsicTrap: {
            if (stack.empty()) return Trap("INTRINSIC trap stack underflow");
            int32_t code = UnpackI32(Pop(stack));
            return Trap("INTRINSIC trap code=" + std::to_string(code));
          }
          case kIntrinsicBreakpoint:
            break;
          case kIntrinsicLogI32:
          case kIntrinsicLogI64:
          case kIntrinsicLogF32:
          case kIntrinsicLogF64:
          case kIntrinsicLogRef:
            if (stack.empty()) return Trap("INTRINSIC log stack underflow");
            Pop(stack);
            break;
          case kIntrinsicAbsI32: {
            if (stack.empty()) return Trap("INTRINSIC abs_i32 stack underflow");
            int32_t value = UnpackI32(Pop(stack));
            Push(stack, PackI32(value < 0 ? -value : value));
            break;
          }
          case kIntrinsicAbsI64: {
            if (stack.empty()) return Trap("INTRINSIC abs_i64 stack underflow");
            int64_t value = UnpackI64(Pop(stack));
            Push(stack, PackI64(value < 0 ? -value : value));
            break;
          }
          case kIntrinsicMinI32:
          case kIntrinsicMaxI32: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max i32 stack underflow");
            int32_t b = UnpackI32(Pop(stack));
            int32_t a = UnpackI32(Pop(stack));
            int32_t out = (id == kIntrinsicMinI32) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackI32(out));
            break;
          }
          case kIntrinsicMinI64:
          case kIntrinsicMaxI64: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max i64 stack underflow");
            int64_t b = UnpackI64(Pop(stack));
            int64_t a = UnpackI64(Pop(stack));
            int64_t out = (id == kIntrinsicMinI64) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackI64(out));
            break;
          }
          case kIntrinsicMinF32:
          case kIntrinsicMaxF32: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max f32 stack underflow");
            float b = UnpackF32(Pop(stack));
            float a = UnpackF32(Pop(stack));
            float out = (id == kIntrinsicMinF32) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackF32(out));
            break;
          }
          case kIntrinsicMinF64:
          case kIntrinsicMaxF64: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max f64 stack underflow");
            double b = UnpackF64(Pop(stack));
            double a = UnpackF64(Pop(stack));
            double out = (id == kIntrinsicMinF64) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackF64(out));
            break;
          }
          case kIntrinsicSqrtF32: {
            if (stack.empty()) return Trap("INTRINSIC sqrt f32 stack underflow");
            float v = UnpackF32(Pop(stack));
            float out = static_cast<float>(std::sqrt(v));
            Push(stack, PackF32(out));
            break;
          }
          case kIntrinsicSqrtF64: {
            if (stack.empty()) return Trap("INTRINSIC sqrt f64 stack underflow");
            double v = UnpackF64(Pop(stack));
            double out = std::sqrt(v);
            Push(stack, PackF64(out));
            break;
          }
          case kIntrinsicMonoNs:
          case kIntrinsicWallNs: {
            const int64_t ns = (id == kIntrinsicMonoNs)
                                   ? Simple::VM::Native::Time::MonotonicNs()
                                   : Simple::VM::Native::Time::WallNs();
            Push(stack, PackI64(ns));
            break;
          }
          case kIntrinsicRandU32:
            Push(stack, PackI32(0));
            break;
          case kIntrinsicRandU64:
            Push(stack, PackI64(0));
            break;
          case kIntrinsicWriteStdout:
          case kIntrinsicWriteStderr:
            if (stack.size() < 2) return Trap("INTRINSIC write stack underflow");
            Pop(stack); // length
            Pop(stack); // ref
            break;
          case kIntrinsicPrintAny: {
            if (stack.size() < 2) return Trap("INTRINSIC print_any stack underflow");
            uint32_t tag = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            Slot value = Pop(stack);
            std::string print_error;
            if (!Simple::VM::Runtime::PrintAny(heap, tag, value, &print_error)) return Trap(print_error);
            std::fflush(stdout);
            break;
          }
          case kIntrinsicStrI32: {
            if (stack.empty()) return Trap("INTRINSIC str_i32 stack underflow");
            int32_t value = UnpackI32(Pop(stack));
            uint32_t handle = CreateString(heap, AsciiToU16(std::to_string(value)));
            if (handle == kNullRef) return Trap("INTRINSIC str_i32 allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicStrI64: {
            if (stack.empty()) return Trap("INTRINSIC str_i64 stack underflow");
            int64_t value = UnpackI64(Pop(stack));
            uint32_t handle = CreateString(heap, AsciiToU16(std::to_string(value)));
            if (handle == kNullRef) return Trap("INTRINSIC str_i64 allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicStrU32: {
            if (stack.empty()) return Trap("INTRINSIC str_u32 stack underflow");
            uint32_t value = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            uint32_t handle = CreateString(heap, AsciiToU16(std::to_string(value)));
            if (handle == kNullRef) return Trap("INTRINSIC str_u32 allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicStrU64: {
            if (stack.empty()) return Trap("INTRINSIC str_u64 stack underflow");
            uint64_t value = static_cast<uint64_t>(UnpackI64(Pop(stack)));
            uint32_t handle = CreateString(heap, AsciiToU16(std::to_string(value)));
            if (handle == kNullRef) return Trap("INTRINSIC str_u64 allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicStrF32: {
            if (stack.empty()) return Trap("INTRINSIC str_f32 stack underflow");
            float value = UnpackF32(Pop(stack));
            uint32_t handle = CreateString(heap, AsciiToU16(std::to_string(value)));
            if (handle == kNullRef) return Trap("INTRINSIC str_f32 allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicStrF64: {
            if (stack.empty()) return Trap("INTRINSIC str_f64 stack underflow");
            double value = UnpackF64(Pop(stack));
            uint32_t handle = CreateString(heap, AsciiToU16(std::to_string(value)));
            if (handle == kNullRef) return Trap("INTRINSIC str_f64 allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicStrBool: {
            if (stack.empty()) return Trap("INTRINSIC str_bool stack underflow");
            bool value = UnpackI32(Pop(stack)) != 0;
            uint32_t handle = CreateString(heap, AsciiToU16(value ? "true" : "false"));
            if (handle == kNullRef) return Trap("INTRINSIC str_bool allocation failed");
            Push(stack, PackRef(handle));
            break;
          }
          case kIntrinsicDlCallI8: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i8 stack underflow");
            int8_t b = static_cast<int8_t>(UnpackI32(Pop(stack)));
            int8_t a = static_cast<int8_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_i8 null ptr");
            using Fn = int8_t (*)(int8_t, int8_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallI16: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i16 stack underflow");
            int16_t b = static_cast<int16_t>(UnpackI32(Pop(stack)));
            int16_t a = static_cast<int16_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_i16 null ptr");
            using Fn = int16_t (*)(int16_t, int16_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallI32: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i32 stack underflow");
            int32_t b = UnpackI32(Pop(stack));
            int32_t a = UnpackI32(Pop(stack));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_i32 null ptr");
            using Fn = int32_t (*)(int32_t, int32_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(fn(a, b)));
            break;
          }
          case kIntrinsicDlCallI64: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i64 stack underflow");
            int64_t b = UnpackI64(Pop(stack));
            int64_t a = UnpackI64(Pop(stack));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_i64 null ptr");
            using Fn = int64_t (*)(int64_t, int64_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI64(fn(a, b)));
            break;
          }
          case kIntrinsicDlCallU8: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u8 stack underflow");
            uint8_t b = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            uint8_t a = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_u8 null ptr");
            using Fn = uint8_t (*)(uint8_t, uint8_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallU16: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u16 stack underflow");
            uint16_t b = static_cast<uint16_t>(UnpackI32(Pop(stack)));
            uint16_t a = static_cast<uint16_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_u16 null ptr");
            using Fn = uint16_t (*)(uint16_t, uint16_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallU32: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u32 stack underflow");
            uint32_t b = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            uint32_t a = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_u32 null ptr");
            using Fn = uint32_t (*)(uint32_t, uint32_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallU64: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u64 stack underflow");
            uint64_t b = static_cast<uint64_t>(UnpackI64(Pop(stack)));
            uint64_t a = static_cast<uint64_t>(UnpackI64(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_u64 null ptr");
            using Fn = uint64_t (*)(uint64_t, uint64_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI64(static_cast<int64_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallF32: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_f32 stack underflow");
            float b = UnpackF32(Pop(stack));
            float a = UnpackF32(Pop(stack));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_f32 null ptr");
            using Fn = float (*)(float, float);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            float out = fn(a, b);
            Push(stack, PackF32(out));
            break;
          }
          case kIntrinsicDlCallF64: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_f64 stack underflow");
            double b = UnpackF64(Pop(stack));
            double a = UnpackF64(Pop(stack));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_f64 null ptr");
            using Fn = double (*)(double, double);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            double out = fn(a, b);
            Push(stack, PackF64(out));
            break;
          }
          case kIntrinsicDlCallBool: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_bool stack underflow");
            bool b = (UnpackI32(Pop(stack)) != 0);
            bool a = (UnpackI32(Pop(stack)) != 0);
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_bool null ptr");
            using Fn = bool (*)(bool, bool);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(fn(a, b) ? 1 : 0));
            break;
          }
          case kIntrinsicDlCallChar: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_char stack underflow");
            uint8_t b = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            uint8_t a = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_char null ptr");
            using Fn = uint8_t (*)(uint8_t, uint8_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallStr0: {
            if (stack.empty()) return Trap("INTRINSIC dl_call_str0 stack underflow");
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("System.FFI.call_str0 null ptr");
            using Fn = const char* (*)();
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            const char* out = fn();
            if (!out) {
              Push(stack, PackRef(kNullRef));
              break;
            }
            uint32_t handle = CreateString(heap, AsciiToU16(out));
            Push(stack, PackRef(handle));
            break;
          }
          default:
            return Trap("INTRINSIC not supported id=" + std::to_string(id));
        }
        break;
      }
      case OpCode::SysCall: {
        uint32_t id = ReadU32(module.code, pc);
        return Trap("SYS_CALL not supported id=" + std::to_string(id));
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
        Push(stack, PackF32(out));
        break;
      }
      case OpCode::NegF32: {
        Slot a = Pop(stack);
        float out = -BitsToF32(static_cast<uint32_t>(a));
        Push(stack, PackF32(out));
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
        Push(stack, PackF32(out));
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
        Push(stack, PackF64(out));
        break;
      }
      case OpCode::NegF64: {
        Slot a = Pop(stack);
        double out = -BitsToF64(static_cast<uint64_t>(a));
        Push(stack, PackF64(out));
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
        Push(stack, PackF64(out));
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
        if (locals != current.locals_count) return Trap("ENTER local count mismatch");
        break;
      }
      case OpCode::Leave:
        break;
      case OpCode::Call:
      case OpCode::CallImport:
      case OpCode::CallNative: {
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

        call_args.resize(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          call_args[static_cast<size_t>(i)] = Pop(stack);
        }
        if (func_id < module.function_is_import.size() && module.function_is_import[func_id]) {
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!Simple::VM::Runtime::DispatchImportCallByName(module, options, native_registry, heap, file_handles, resource_registry, promise_registry, dl_last_error, func_id, call_args, ret, has_ret, error)) {
            return Trap(error);
          }
          if (has_ret) Push(stack, ret);
          break;
        }
        if (enable_jit && !llvm_rejected[func_id]) {
          jit_dispatch_counts[func_id] += 1;
          Simple::VM::Jit::LlvmJitBackend llvm_backend({true, true});
          Slot ret = 0;
          bool has_ret = false;
          std::string llvm_reason;
          if (llvm_backend.TryRunFunctionWithRuntime(module, func_id, call_args, &heap, &globals, &options, ret, has_ret, llvm_reason)) {
            jit_tiers[func_id] = JitTier::Tier1;
            jit_compiled_exec_counts[func_id] += 1;
            jit_tier1_exec_counts[func_id] += 1;
            if (has_ret) Push(stack, ret);
            break;
          }
          record_llvm_reject(func_id, llvm_reason);
          if (!is_llvm_unsupported(llvm_reason)) return Trap(llvm_reason);
        }

        current.return_pc = pc;
        current.stack_base = stack.size();
        if (!Simple::VM::Runtime::CheckCallDepthLimit(limits, call_stack.size())) {
          return Trap("runtime limit exceeded: call depth");
        }
        call_stack.push_back(current);
        call_counts[func_id] += 1;
        current = Simple::VM::Interpreter::BuildFrame(module, locals_arena, func_id, pc, stack.size(), kNullRef);
        for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) {
          locals_arena[current.locals_base + i] = call_args[i];
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
            bool closure_found = false;
            for (size_t i = 0; i < module.functions.size(); ++i) {
              if (module.functions[i].method_id == method_id) {
                func_index = static_cast<int64_t>(i);
                closure_found = true;
                break;
              }
            }
            if (!closure_found) return Trap("CALL_INDIRECT closure method not found");
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

        call_args.resize(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          call_args[static_cast<size_t>(i)] = Pop(stack);
        }
        if (static_cast<size_t>(func_index) < module.function_is_import.size() &&
            module.function_is_import[static_cast<size_t>(func_index)]) {
          if (closure_ref != kNullRef) {
            return Trap("CALL_INDIRECT import closure unsupported");
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!Simple::VM::Runtime::DispatchImportCallByName(module, options, native_registry, heap, file_handles, resource_registry, promise_registry, dl_last_error, static_cast<uint32_t>(func_index), call_args, ret, has_ret, error)) {
            return Trap(error);
          }
          if (has_ret) Push(stack, ret);
          break;
        }

        size_t target_index = static_cast<size_t>(func_index);
        if (enable_jit && !llvm_rejected[target_index]) {
          jit_dispatch_counts[target_index] += 1;
          Simple::VM::Jit::LlvmJitBackend llvm_backend({true, true});
          Slot ret = 0;
          bool has_ret = false;
          std::string llvm_reason;
          if (llvm_backend.TryRunFunctionWithRuntime(module, target_index, call_args, &heap, &globals, &options, ret, has_ret, llvm_reason)) {
            jit_tiers[target_index] = JitTier::Tier1;
            jit_compiled_exec_counts[target_index] += 1;
            jit_tier1_exec_counts[target_index] += 1;
            if (has_ret) Push(stack, ret);
            break;
          }
          record_llvm_reject(target_index, llvm_reason);
          if (!is_llvm_unsupported(llvm_reason)) return Trap(llvm_reason);
        }

        current.return_pc = pc;
        current.stack_base = stack.size();
        if (!Simple::VM::Runtime::CheckCallDepthLimit(limits, call_stack.size())) {
          return Trap("runtime limit exceeded: call depth");
        }
        call_stack.push_back(current);
        call_counts[static_cast<size_t>(func_index)] += 1;
        current = Simple::VM::Interpreter::BuildFrame(module, locals_arena, static_cast<size_t>(func_index), pc, stack.size(), closure_ref);
        for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) {
          locals_arena[current.locals_base + i] = call_args[i];
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
        const auto& func = module.functions[func_id];
        if (func.method_id >= module.methods.size()) return Trap("TAILCALL invalid method id");
        const auto& method = module.methods[func.method_id];
        if (method.sig_id >= module.sigs.size()) return Trap("TAILCALL invalid signature id");
        const auto& sig = module.sigs[method.sig_id];
        if (arg_count != sig.param_count) return Trap("TAILCALL arg count mismatch");
        if (stack.size() < arg_count) return Trap("TAILCALL stack underflow");

        call_args.resize(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          call_args[static_cast<size_t>(i)] = Pop(stack);
        }
        if (func_id < module.function_is_import.size() && module.function_is_import[func_id]) {
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!Simple::VM::Runtime::DispatchImportCallByName(module, options, native_registry, heap, file_handles, resource_registry, promise_registry, dl_last_error, func_id, call_args, ret, has_ret, error)) {
            return Trap(error);
          }
          if (call_stack.empty()) {
            ExecResult result;
            result.status = ExecStatus::Halted;
            if (has_ret) result.exit_code = UnpackI32(ret);
            return Simple::VM::Runtime::AttachExecutionStats(result, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
          }
          Simple::VM::Interpreter::FrameState caller = call_stack.back();
          call_stack.pop_back();
          stack.resize(caller.stack_base);
          locals_arena.resize(caller.locals_base + caller.locals_count);
          if (has_ret) Push(stack, ret);
          current = caller;
          pc = current.return_pc;
          const auto& current_func = module.functions[current.func_index];
          func_start = current_func.code_offset;
          end = func_start + current_func.code_size;
          break;
        }

        if (enable_jit && !llvm_rejected[func_id]) {
          jit_dispatch_counts[func_id] += 1;
          Simple::VM::Jit::LlvmJitBackend llvm_backend({true, true});
          Slot ret = 0;
          bool has_ret = false;
          std::string llvm_reason;
          if (llvm_backend.TryRunFunctionWithRuntime(module, func_id, call_args, &heap, &globals, &options, ret, has_ret, llvm_reason)) {
            jit_tiers[func_id] = JitTier::Tier1;
            jit_compiled_exec_counts[func_id] += 1;
            jit_tier1_exec_counts[func_id] += 1;
            if (call_stack.empty()) {
              ExecResult result;
              result.status = ExecStatus::Halted;
              if (has_ret) result.exit_code = UnpackI32(ret);
              return Simple::VM::Runtime::AttachExecutionStats(result, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
            }
            Simple::VM::Interpreter::FrameState caller = call_stack.back();
            call_stack.pop_back();
            stack.resize(caller.stack_base);
            locals_arena.resize(caller.locals_base + caller.locals_count);
            if (has_ret) Push(stack, ret);
            current = caller;
            pc = current.return_pc;
            const auto& current_func = module.functions[current.func_index];
            func_start = current_func.code_offset;
            end = func_start + current_func.code_size;
            break;
          }
          record_llvm_reject(func_id, llvm_reason);
          if (!is_llvm_unsupported(llvm_reason)) return Trap(llvm_reason);
        }

        size_t return_pc = current.return_pc;
        size_t stack_base = current.stack_base;
        locals_arena.resize(current.locals_base);
        stack.resize(stack_base);
        call_counts[func_id] += 1;
        current = Simple::VM::Interpreter::BuildFrame(module, locals_arena, func_id, return_pc, stack_base, kNullRef);
        for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) {
          locals_arena[current.locals_base + i] = call_args[i];
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
        Push(stack, PackF32(out));
        break;
      }
      case OpCode::ConvI32ToF64: {
        Slot v = Pop(stack);
        double out = static_cast<double>(UnpackI32(v));
        Push(stack, PackF64(out));
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
        Push(stack, PackF64(out));
        break;
      }
      case OpCode::ConvF64ToF32: {
        Slot v = Pop(stack);
        float out = static_cast<float>(BitsToF64(static_cast<uint64_t>(v)));
        Push(stack, PackF32(out));
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
          return Simple::VM::Runtime::AttachExecutionStats(result, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
        }
        Simple::VM::Interpreter::FrameState caller = call_stack.back();
        call_stack.pop_back();
        stack.resize(caller.stack_base);
        locals_arena.resize(caller.locals_base + caller.locals_count);
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
  return Simple::VM::Runtime::AttachExecutionStats(result, jit_tiers, call_counts, opcode_counts, compile_counts, func_opcode_counts, compile_ticks_tier0, compile_ticks_tier1, jit_dispatch_counts, jit_compiled_exec_counts, jit_tier1_exec_counts, llvm_reject_counts, llvm_reject_reasons);
}

} // namespace Simple::VM
