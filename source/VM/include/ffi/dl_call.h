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

struct DynamicDlAbiValidation {
  bool abi_valid = false;
  bool vm_marshal_supported = false;
  bool may_allocate = false;
  bool may_block = true;
  bool needs_roots = false;
  bool jit_helper_safe = false;
  bool jit_loop_safe = false;
  std::string reason;
};

DynamicDlAbiValidation AnalyzeDynamicDlCallSignature(const Simple::Byte::SbcModule& module,
                                                    uint32_t ret_type_id,
                                                    bool has_ret,
                                                    const std::vector<uint32_t>& arg_type_ids);

DynamicDlAbiValidation AnalyzeDynamicDlFunctionSignature(const Simple::Byte::SbcModule& module,
                                                        uint32_t ret_type_id,
                                                        bool has_ret,
                                                        const std::vector<uint32_t>& param_type_ids);

bool ValidateDynamicDlCallSignature(const Simple::Byte::SbcModule& module,
                                    uint32_t ret_type_id,
                                    bool has_ret,
                                    const std::vector<uint32_t>& arg_type_ids,
                                    std::string* out_error);

bool ValidateDynamicDlFunctionSignature(const Simple::Byte::SbcModule& module,
                                        uint32_t ret_type_id,
                                        bool has_ret,
                                        const std::vector<uint32_t>& param_type_ids,
                                        std::string* out_error);

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
