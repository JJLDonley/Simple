#pragma once

#include <cstdint>

#include "sbc_types.h"

namespace Simple::VM::Runtime {

enum class AbiClass {
  Invalid,
  Void,
  Scalar,
  Float,
  Ref,
  Handle,
  Aggregate,
  Variant,
  Promise,
  Opaque,
};

struct AbiTypeInfo {
  AbiClass abi_class = AbiClass::Invalid;
  uint32_t size = 0;
  uint32_t align = 0;
  bool native_callable = false;
  bool external_ffi_callable = false;
};

AbiTypeInfo GetPrimitiveAbiTypeInfo(Simple::Byte::TypeKind kind);
uint32_t AlignAbiOffset(uint32_t offset, uint32_t alignment);
bool IsSmallAbiAggregate(uint32_t size, bool contains_references);

} // namespace Simple::VM::Runtime
