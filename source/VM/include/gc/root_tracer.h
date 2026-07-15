#ifndef SIMPLE_VM_GC_ROOT_TRACER_H
#define SIMPLE_VM_GC_ROOT_TRACER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "heap.h"

namespace Simple::VM::Gc {

using Slot = uint64_t;

struct RootTraceFrame {
  size_t locals_base = 0;
  uint16_t locals_count = 0;
  const std::vector<uint8_t>* locals_ref_bits = nullptr;
  uint32_t closure_ref = HeapLayout::kNullRef;
  uint32_t completing_promise_ref = HeapLayout::kNullRef;
};

struct RootTraceContext {
  Heap* heap = nullptr;
  const std::vector<Slot>* globals = nullptr;
  const std::vector<uint8_t>* global_ref_bits = nullptr;
  const std::vector<Slot>* stack = nullptr;
  const std::vector<uint8_t>* stack_ref_bits = nullptr;
  size_t stack_height = 0;
  const std::vector<RootTraceFrame>* call_stack = nullptr;
  const RootTraceFrame* current = nullptr;
  const std::vector<Slot>* locals_arena = nullptr;
};

void TraceRoots(const RootTraceContext& context);

} // namespace Simple::VM::Gc

#endif // SIMPLE_VM_GC_ROOT_TRACER_H
