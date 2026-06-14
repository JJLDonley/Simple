#ifndef SIMPLE_VM_RUNTIME_PRINT_ANY_H
#define SIMPLE_VM_RUNTIME_PRINT_ANY_H

#include <cstdint>
#include <string>

#include "heap.h"
#include "interpreter/stack.h"

namespace Simple::VM::Runtime {

bool PrintAny(Heap& heap, uint32_t tag, Simple::VM::Interpreter::Slot value, std::string* out_error);

} // namespace Simple::VM::Runtime

#endif // SIMPLE_VM_RUNTIME_PRINT_ANY_H
