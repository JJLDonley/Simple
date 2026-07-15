#include "gc/stack_map_collection.h"

namespace Simple::VM::Gc {

const Simple::Byte::StackMap* FindVerifiedStackMap(const Simple::Byte::VerifyResult& verify_result,
                                                   size_t func_index,
                                                   size_t pc_value) {
  if (func_index >= verify_result.methods.size()) return nullptr;
  const auto& maps = verify_result.methods[func_index].stack_maps;
  for (const auto& map : maps) {
    if (map.pc == pc_value) return &map;
  }
  return nullptr;
}

void MaybeCollectWithStackMap(bool have_meta,
                              size_t op_counter,
                              size_t pc,
                              const Simple::Byte::VerifyResult& verify_result,
                              Heap& heap,
                              const std::vector<Slot>& globals,
                              const std::vector<Slot>& stack,
                              const std::vector<Interpreter::FrameState>& call_stack,
                              const Interpreter::FrameState& current,
                              const std::vector<Slot>& locals_arena) {
  if (!have_meta) return;
  if (op_counter % 1000 != 0) return;
  const Simple::Byte::StackMap* stack_map = FindVerifiedStackMap(verify_result, current.func_index, pc);
  if (!stack_map) return;
  heap.ResetMarks();
  std::vector<RootTraceFrame> root_call_stack;
  root_call_stack.reserve(call_stack.size());
  for (const auto& frame : call_stack) {
    if (frame.func_index >= verify_result.methods.size()) continue;
    root_call_stack.push_back({frame.locals_base, frame.locals_count,
                               &verify_result.methods[frame.func_index].locals_ref_bits,
                               frame.closure_ref, frame.completing_promise_ref});
  }
  RootTraceFrame root_current{};
  const RootTraceFrame* current_root = nullptr;
  if (current.func_index < verify_result.methods.size()) {
    root_current = {current.locals_base, current.locals_count,
                    &verify_result.methods[current.func_index].locals_ref_bits,
                    current.closure_ref, current.completing_promise_ref};
    current_root = &root_current;
  }
  RootTraceContext root_context;
  root_context.heap = &heap;
  root_context.globals = &globals;
  root_context.global_ref_bits = &verify_result.globals_ref_bits;
  root_context.stack = &stack;
  root_context.stack_ref_bits = &stack_map->ref_bits;
  root_context.stack_height = stack_map->stack_height;
  root_context.call_stack = &root_call_stack;
  root_context.current = current_root;
  root_context.locals_arena = &locals_arena;
  TraceRoots(root_context);
  heap.Sweep();
}

} // namespace Simple::VM::Gc
