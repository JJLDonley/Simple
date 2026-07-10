#include "jit/call_context.h"

#include <utility>

#include "runtime/values.h"

namespace Simple::VM::Jit {
namespace {

thread_local std::vector<const std::vector<uint32_t>*> g_jit_root_frames;

} // namespace

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

void MarkJitSafepoint(JitCallContext* context,
                      uint32_t function_index,
                      uint32_t pc,
                      bool may_block,
                      bool may_allocate) {
  if (!context) return;
  context->safepoint.active = true;
  context->safepoint.function_index = function_index;
  context->safepoint.pc = pc;
  context->safepoint.may_block = may_block;
  context->safepoint.may_allocate = may_allocate;
}

void ClearJitSafepoint(JitCallContext* context) {
  if (!context) return;
  context->safepoint = JitCallSafepoint{};
}

bool IsJitRootType(const Simple::Byte::SbcModule& module, uint32_t type_id) {
  if (type_id >= module.types.size()) return false;
  switch (static_cast<Simple::Byte::TypeKind>(module.types[type_id].kind)) {
    case Simple::Byte::TypeKind::Ref:
    case Simple::Byte::TypeKind::String:
    case Simple::Byte::TypeKind::Array:
    case Simple::Byte::TypeKind::List:
    case Simple::Byte::TypeKind::Function:
      return true;
    default:
      return false;
  }
}

void PublishJitRootSlotsByMask(JitCallContext* context,
                               const std::vector<Slot>& slots,
                               uint64_t ref_mask) {
  if (!context) return;
  for (size_t i = 0; i < slots.size() && i < 64; ++i) {
    if ((ref_mask & (uint64_t{1} << i)) == 0) continue;
    if (!Simple::VM::Runtime::IsNullRef(slots[i])) RegisterJitRoot(context, Simple::VM::Runtime::UnpackRef(slots[i]));
  }
}

void PushJitRootFrame(const std::vector<uint32_t>* root_refs) {
  if (!root_refs) return;
  g_jit_root_frames.push_back(root_refs);
}

void PopJitRootFrame(const std::vector<uint32_t>* root_refs) {
  if (g_jit_root_frames.empty()) return;
  if (g_jit_root_frames.back() == root_refs) {
    g_jit_root_frames.pop_back();
    return;
  }
  for (auto it = g_jit_root_frames.rbegin(); it != g_jit_root_frames.rend(); ++it) {
    if (*it == root_refs) {
      g_jit_root_frames.erase(std::next(it).base());
      return;
    }
  }
}

void MarkPublishedJitRoots(Heap& heap) {
  for (const std::vector<uint32_t>* refs : g_jit_root_frames) {
    if (!refs) continue;
    for (uint32_t ref : *refs) {
      if (ref != Simple::VM::HeapLayout::kNullRef) heap.Mark(ref);
    }
  }
}

bool PublishJitRootsFromContext(JitCallContext* context,
                                const Simple::Byte::SbcModule& module,
                                const std::vector<uint32_t>& arg_type_ids,
                                const std::vector<uint32_t>& local_type_ids,
                                const std::vector<uint32_t>& stack_type_ids) {
  if (!context) return false;
  if (arg_type_ids.size() > context->args.size() || local_type_ids.size() > context->locals.size() ||
      stack_type_ids.size() > context->operand_stack.size()) {
    return false;
  }
  auto publish_slot = [&](Slot slot, uint32_t type_id) -> bool {
    if (type_id >= module.types.size()) return false;
    if (!IsJitRootType(module, type_id)) return true;
    if (!Simple::VM::Runtime::IsNullRef(slot)) RegisterJitRoot(context, Simple::VM::Runtime::UnpackRef(slot));
    return true;
  };
  ClearJitRoots(context);
  for (size_t i = 0; i < arg_type_ids.size(); ++i) {
    if (!publish_slot(context->args[i], arg_type_ids[i])) return false;
  }
  for (size_t i = 0; i < local_type_ids.size(); ++i) {
    if (!publish_slot(context->locals[i], local_type_ids[i])) return false;
  }
  for (size_t i = 0; i < stack_type_ids.size(); ++i) {
    if (!publish_slot(context->operand_stack[i], stack_type_ids[i])) return false;
  }
  return true;
}

} // namespace Simple::VM::Jit
