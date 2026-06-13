#include "interpreter/frames.h"

namespace Simple::VM::Interpreter {

FrameState MakeFrame(size_t func_index, size_t return_pc, size_t stack_base, uint32_t closure_ref) {
  FrameState frame;
  frame.func_index = func_index;
  frame.return_pc = return_pc;
  frame.stack_base = stack_base;
  frame.closure_ref = closure_ref;
  return frame;
}

} // namespace Simple::VM::Interpreter
