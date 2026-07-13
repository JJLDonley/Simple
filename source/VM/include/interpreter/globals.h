#ifndef SIMPLE_VM_INTERPRETER_GLOBALS_H
#define SIMPLE_VM_INTERPRETER_GLOBALS_H

#include <cstddef>
#include <cstdint>

#include "heap.h"
#include "interpreter/stack.h"
#include "sbc_types.h"

namespace Simple::VM::Interpreter {

bool LoadConstStringSlot(const Simple::Byte::SbcModule& module, Heap& heap, uint32_t const_id, Slot& out_value);
bool IsRefLikeGlobal(const Simple::Byte::SbcModule& module, size_t global_index);

} // namespace Simple::VM::Interpreter

#endif // SIMPLE_VM_INTERPRETER_GLOBALS_H
