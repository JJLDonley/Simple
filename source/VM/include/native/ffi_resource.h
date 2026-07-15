#ifndef SIMPLE_VM_NATIVE_FFI_RESOURCE_H
#define SIMPLE_VM_NATIVE_FFI_RESOURCE_H

#include <cstdint>

#include "native/resource_registry.h"

namespace Simple::VM::Native {

struct FfiSymbolResource {
  NativeHandleId library;
  uintptr_t address = 0;
};

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_FFI_RESOURCE_H
