#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "heap.h"
#include "interpreter/stack.h"

namespace Simple::VM::Jit {

using Slot = Simple::VM::Interpreter::Slot;

enum class JitCallTrapKind {
  None,
  Trap,
  Fallback,
  Unsupported,
};

struct JitCallTrap {
  JitCallTrapKind kind = JitCallTrapKind::None;
  std::string message;
};

struct JitCallContext {
  std::vector<Slot> args;
  std::vector<Slot> operand_stack;
  std::vector<Slot> locals;
  std::vector<Slot> spills;
  std::vector<Slot>* globals = nullptr;
  Heap* heap = nullptr;
  Slot return_value = 0;
  bool has_return = false;
  JitCallTrap trap;
  std::vector<uint32_t> root_refs;
};

bool JitArg(const JitCallContext& context, size_t index, Slot* out);
bool JitStackSlot(const JitCallContext& context, size_t index, Slot* out);
bool PushJitStack(JitCallContext* context, Slot value);
bool PopJitStack(JitCallContext* context, Slot* out);
bool JitLocal(const JitCallContext& context, size_t index, Slot* out);
bool SetJitLocal(JitCallContext* context, size_t index, Slot value);
bool JitGlobal(const JitCallContext& context, size_t index, Slot* out);
bool SetJitGlobal(JitCallContext* context, size_t index, Slot value);
void SetJitReturn(JitCallContext* context, Slot value);
void ClearJitReturn(JitCallContext* context);
void SetJitTrap(JitCallContext* context, JitCallTrapKind kind, std::string message);
void RegisterJitRoot(JitCallContext* context, uint32_t ref);
void ClearJitRoots(JitCallContext* context);

} // namespace Simple::VM::Jit
