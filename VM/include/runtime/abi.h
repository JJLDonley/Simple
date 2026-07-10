#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

struct AbiFieldLayout {
  uint32_t offset = 0;
  AbiTypeInfo type;
};

struct AbiAggregateLayout {
  uint32_t size = 0;
  uint32_t align = 1;
  bool contains_references = false;
  bool native_callable = true;
  bool external_ffi_callable = true;
  bool pass_by_value = true;
  std::vector<AbiFieldLayout> fields;
};

AbiTypeInfo GetPrimitiveAbiTypeInfo(Simple::Byte::TypeKind kind);
uint32_t AlignAbiOffset(uint32_t offset, uint32_t alignment);
bool IsReferenceAbiClass(AbiClass abi_class);
bool IsSmallAbiAggregate(uint32_t size, bool contains_references);
AbiAggregateLayout ComputeStableAggregateLayout(const std::vector<AbiTypeInfo>& fields);
bool ValidateAbiCallableSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                  Simple::Byte::TypeKind result_type,
                                  bool external_ffi,
                                  std::string* error);

} // namespace Simple::VM::Runtime
