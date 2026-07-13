#include "ffi/dl_runtime.h"

#include "platform/platform.h"

namespace Simple::VM::Ffi::DlRuntime {

int64_t Open(const std::string& path, std::string* out_error) {
  return Simple::Platform::OpenDynamicLibrary(path, out_error);
}

int64_t Symbol(int64_t handle_bits, const std::string& name, std::string* out_error) {
  return Simple::Platform::FindDynamicSymbol(handle_bits, name, out_error);
}

bool Close(int64_t handle_bits, std::string* out_error) {
  return Simple::Platform::CloseDynamicLibrary(handle_bits, out_error);
}

} // namespace Simple::VM::Ffi::DlRuntime
