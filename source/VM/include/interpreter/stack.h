#ifndef SIMPLE_VM_INTERPRETER_STACK_H
#define SIMPLE_VM_INTERPRETER_STACK_H

#include <cstdint>
#include <vector>

namespace Simple::VM::Interpreter {

using Slot = uint64_t;

Slot Pop(std::vector<Slot>& stack);
void Push(std::vector<Slot>& stack, Slot value);
Slot Peek(const std::vector<Slot>& stack);

} // namespace Simple::VM::Interpreter

#endif // SIMPLE_VM_INTERPRETER_STACK_H
