#include "jit/call_context.h"

#include <utility>

namespace Simple::VM::Jit {

bool JitArg(const JitCallContext& context, size_t index, Slot* out) {
  if (!out || index >= context.args.size()) return false;
  *out = context.args[index];
  return true;
}

bool JitStackSlot(const JitCallContext& context, size_t index, Slot* out) {
  if (!out || index >= context.operand_stack.size()) return false;
  *out = context.operand_stack[index];
  return true;
}

bool PushJitStack(JitCallContext* context, Slot value) {
  if (!context) return false;
  context->operand_stack.push_back(value);
  return true;
}

bool PopJitStack(JitCallContext* context, Slot* out) {
  if (!context || !out || context->operand_stack.empty()) return false;
  *out = context->operand_stack.back();
  context->operand_stack.pop_back();
  return true;
}

bool JitLocal(const JitCallContext& context, size_t index, Slot* out) {
  if (!out || index >= context.locals.size()) return false;
  *out = context.locals[index];
  return true;
}

bool SetJitLocal(JitCallContext* context, size_t index, Slot value) {
  if (!context || index >= context->locals.size()) return false;
  context->locals[index] = value;
  return true;
}

bool JitGlobal(const JitCallContext& context, size_t index, Slot* out) {
  if (!out || !context.globals || index >= context.globals->size()) return false;
  *out = (*context.globals)[index];
  return true;
}

bool SetJitGlobal(JitCallContext* context, size_t index, Slot value) {
  if (!context || !context->globals || index >= context->globals->size()) return false;
  (*context->globals)[index] = value;
  return true;
}

void SetJitReturn(JitCallContext* context, Slot value) {
  if (!context) return;
  context->return_value = value;
  context->has_return = true;
}

void ClearJitReturn(JitCallContext* context) {
  if (!context) return;
  context->return_value = 0;
  context->has_return = false;
}

void SetJitTrap(JitCallContext* context, JitCallTrapKind kind, std::string message) {
  if (!context) return;
  context->trap.kind = kind;
  context->trap.message = std::move(message);
}

void RegisterJitRoot(JitCallContext* context, uint32_t ref) {
  if (!context) return;
  context->root_refs.push_back(ref);
}

void ClearJitRoots(JitCallContext* context) {
  if (!context) return;
  context->root_refs.clear();
}

} // namespace Simple::VM::Jit
