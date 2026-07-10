#include "gc/root_tracer.h"

#include "jit/call_context.h"

namespace Simple::VM::Gc {
namespace {

constexpr uint32_t kNullRef = 0xFFFFFFFFu;

uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value & 0xFFFFFFFFu);
}

bool RefBitSet(const std::vector<uint8_t>& bits, size_t index) {
  const size_t byte = index / 8;
  if (byte >= bits.size()) return false;
  return (bits[byte] & static_cast<uint8_t>(1u << (index % 8))) != 0;
}

void MarkSlot(Heap& heap, Slot value) {
  if (!IsNullRef(value)) heap.Mark(UnpackRef(value));
}

void TraceLocals(Heap& heap, const std::vector<Slot>& locals, const RootTraceFrame& frame) {
  if (!frame.locals_ref_bits || frame.locals_base > locals.size()) return;
  const size_t available = locals.size() - frame.locals_base;
  const size_t count = frame.locals_count < available ? frame.locals_count : available;
  for (size_t i = 0; i < count; ++i) {
    if (RefBitSet(*frame.locals_ref_bits, i)) MarkSlot(heap, locals[frame.locals_base + i]);
  }
}

} // namespace

bool IsNullRef(Slot value) {
  return UnpackRef(value) == kNullRef;
}

void TraceRoots(const RootTraceContext& context) {
  if (!context.heap) return;
  Heap& heap = *context.heap;
  if (context.globals && context.global_ref_bits) {
    for (size_t i = 0; i < context.globals->size(); ++i) {
      if (RefBitSet(*context.global_ref_bits, i)) MarkSlot(heap, (*context.globals)[i]);
    }
  }
  if (context.stack && context.stack_ref_bits) {
    const size_t count = context.stack_height < context.stack->size() ? context.stack_height : context.stack->size();
    for (size_t i = 0; i < count; ++i) {
      if (RefBitSet(*context.stack_ref_bits, i)) MarkSlot(heap, (*context.stack)[i]);
    }
  }
  if (context.locals_arena) {
    if (context.call_stack) {
      for (const RootTraceFrame& frame : *context.call_stack) TraceLocals(heap, *context.locals_arena, frame);
    }
    if (context.current) TraceLocals(heap, *context.locals_arena, *context.current);
  }
  Simple::VM::Jit::MarkPublishedJitRoots(heap);
}

} // namespace Simple::VM::Gc
