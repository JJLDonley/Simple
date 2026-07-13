#include "interpreter/interpreter.h"

namespace Simple::VM::Interpreter {

InterpreterState MakeInterpreterState(size_t global_count) {
  InterpreterState state;
  state.globals.resize(global_count);
  return state;
}

} // namespace Simple::VM::Interpreter
