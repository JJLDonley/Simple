#include "interpreter/stack.h"

namespace Simple::VM::Interpreter {

Slot Pop(std::vector<Slot>& stack) {
  Slot value = stack.back();
  stack.pop_back();
  return value;
}

void Push(std::vector<Slot>& stack, Slot value) {
  stack.push_back(value);
}

Slot Peek(const std::vector<Slot>& stack) {
  return stack.back();
}

} // namespace Simple::VM::Interpreter
