#ifndef SIMPLE_VM_INTERPRETER_INTERPRETER_H
#define SIMPLE_VM_INTERPRETER_INTERPRETER_H

#include <cstddef>
#include <vector>

#include "interpreter/frames.h"
#include "interpreter/stack.h"

namespace Simple::VM::Interpreter {

struct InterpreterState {
  std::vector<Slot> globals;
  std::vector<Slot> locals_arena;
  std::vector<Slot> stack;
  std::vector<FrameState> call_stack;
  std::vector<Slot> call_args;
};

InterpreterState MakeInterpreterState(size_t global_count);

} // namespace Simple::VM::Interpreter

#endif // SIMPLE_VM_INTERPRETER_INTERPRETER_H
