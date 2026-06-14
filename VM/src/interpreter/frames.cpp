#include "interpreter/frames.h"

#include <algorithm>

namespace Simple::VM::Interpreter {

FrameState MakeFrame(size_t func_index, size_t return_pc, size_t stack_base, uint32_t closure_ref) {
  FrameState frame;
  frame.func_index = func_index;
  frame.return_pc = return_pc;
  frame.stack_base = stack_base;
  frame.closure_ref = closure_ref;
  return frame;
}

size_t AllocateLocalSlots(std::vector<uint64_t>& locals_arena, uint16_t count) {
  const size_t base = locals_arena.size();
  locals_arena.resize(base + count);
  std::fill(locals_arena.begin() + base, locals_arena.end(), 0);
  return base;
}

FrameState BuildFrame(const Simple::Byte::SbcModule& module,
                      std::vector<uint64_t>& locals_arena,
                      size_t func_index,
                      size_t return_pc,
                      size_t stack_base,
                      uint32_t closure_ref) {
  FrameState frame = MakeFrame(func_index, return_pc, stack_base, closure_ref);
  const uint32_t method_id = module.functions[func_index].method_id;
  if (method_id >= module.methods.size()) {
    frame.locals_base = 0;
    frame.locals_count = 0;
    return frame;
  }
  const uint16_t local_count = module.methods[method_id].local_count;
  frame.locals_count = local_count;
  frame.locals_base = AllocateLocalSlots(locals_arena, local_count);
  return frame;
}

} // namespace Simple::VM::Interpreter
