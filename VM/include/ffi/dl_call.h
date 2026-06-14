#ifndef SIMPLE_VM_FFI_DL_CALL_H
#define SIMPLE_VM_FFI_DL_CALL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "heap.h"
#include "interpreter/stack.h"
#include "sbc_types.h"

namespace Simple::VM::Ffi {

bool DispatchDynamicDlCall(int64_t ptr_bits,
                           const Simple::Byte::SbcModule& module,
                           uint32_t ret_type_id,
                           bool has_ret,
                           const std::vector<uint32_t>& arg_type_ids,
                           const std::vector<Simple::VM::Interpreter::Slot>& args,
                           size_t arg_base,
                           Heap& heap,
                           Simple::VM::Interpreter::Slot* out_ret,
                           std::string* out_error);

} // namespace Simple::VM::Ffi

#endif // SIMPLE_VM_FFI_DL_CALL_H
