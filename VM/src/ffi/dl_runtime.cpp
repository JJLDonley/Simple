#include "ffi/dl_runtime.h"

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace Simple::VM::Ffi::DlRuntime {
namespace {

void SetError(std::string* out_error, const std::string& value) {
  if (out_error) *out_error = value;
}

} // namespace

int64_t Open(const std::string& path, std::string* out_error) {
#if defined(_WIN32)
  (void)path;
  SetError(out_error, "core.dl.open is unsupported on windows");
  return 0;
#else
  dlerror();
  void* handle = dlopen(path.c_str(), RTLD_LAZY);
  if (!handle) {
    const char* err = dlerror();
    SetError(out_error, err ? err : "core.dl.open failed");
    return 0;
  }
  if (out_error) out_error->clear();
  return reinterpret_cast<int64_t>(handle);
#endif
}

int64_t Symbol(int64_t handle_bits, const std::string& name, std::string* out_error) {
  if (handle_bits == 0) {
    SetError(out_error, "core.dl.sym null handle");
    return 0;
  }
#if defined(_WIN32)
  (void)name;
  SetError(out_error, "core.dl.sym is unsupported on windows");
  return 0;
#else
  dlerror();
  void* sym_ptr = dlsym(reinterpret_cast<void*>(handle_bits), name.c_str());
  const char* err = dlerror();
  if (err) {
    SetError(out_error, err);
    return 0;
  }
  if (out_error) out_error->clear();
  return reinterpret_cast<int64_t>(sym_ptr);
#endif
}

bool Close(int64_t handle_bits, std::string* out_error) {
  if (handle_bits == 0) {
    SetError(out_error, "core.dl.close null handle");
    return false;
  }
#if defined(_WIN32)
  SetError(out_error, "core.dl.close is unsupported on windows");
  return false;
#else
  int rc = dlclose(reinterpret_cast<void*>(handle_bits));
  if (rc != 0) {
    const char* err = dlerror();
    SetError(out_error, err ? err : "core.dl.close failed");
    return false;
  }
  if (out_error) out_error->clear();
  return true;
#endif
}

} // namespace Simple::VM::Ffi::DlRuntime
