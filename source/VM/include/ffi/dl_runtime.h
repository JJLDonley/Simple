#ifndef SIMPLE_VM_FFI_DL_RUNTIME_H
#define SIMPLE_VM_FFI_DL_RUNTIME_H

#include <cstdint>
#include <string>

namespace Simple::VM::Ffi::DlRuntime {

int64_t Open(const std::string& path, std::string* out_error);
int64_t Symbol(int64_t handle_bits, const std::string& name, std::string* out_error);
bool Close(int64_t handle_bits, std::string* out_error);

} // namespace Simple::VM::Ffi::DlRuntime

#endif // SIMPLE_VM_FFI_DL_RUNTIME_H
