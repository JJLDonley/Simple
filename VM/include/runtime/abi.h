#pragma once

#include <cstddef>
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

enum class AbiStringEncoding {
  Utf8,
};

struct SimpleStringView {
  const char* data = nullptr;
  size_t size = 0;
  AbiStringEncoding encoding = AbiStringEncoding::Utf8;
};

struct SimpleBytesView {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

enum class AbiVariantTag : uint32_t {
  None = 0,
  Some = 1,
  Ok = 1,
  Err = 2,
};

struct AbiVariantValue {
  AbiVariantTag tag = AbiVariantTag::None;
  uint32_t reserved = 0;
  uint64_t payload = 0;
};

static_assert(sizeof(AbiVariantValue) == 16, "ABI variant value must stay 16 bytes");

AbiTypeInfo GetPrimitiveAbiTypeInfo(Simple::Byte::TypeKind kind);
uint32_t AlignAbiOffset(uint32_t offset, uint32_t alignment);
bool IsReferenceAbiClass(AbiClass abi_class);
bool IsSmallAbiAggregate(uint32_t size, bool contains_references);
bool IsValidBorrowedStringView(const SimpleStringView& view);
bool IsValidBorrowedBytesView(const SimpleBytesView& view);
AbiVariantValue MakeAbiOptionNone();
AbiVariantValue MakeAbiOptionSome(uint64_t payload);
AbiVariantValue MakeAbiResultOk(uint64_t payload);
AbiVariantValue MakeAbiResultErr(uint64_t payload);
bool IsAbiOptionSome(const AbiVariantValue& value);
bool IsAbiResultOk(const AbiVariantValue& value);
bool IsAbiResultErr(const AbiVariantValue& value);
AbiAggregateLayout ComputeStableAggregateLayout(const std::vector<AbiTypeInfo>& fields);
bool ValidateAbiCallableSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                  Simple::Byte::TypeKind result_type,
                                  bool external_ffi,
                                  std::string* error);

} // namespace Simple::VM::Runtime
