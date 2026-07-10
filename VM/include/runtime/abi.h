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

enum class AbiPassMode {
  Invalid,
  Void,
  Direct,
  Indirect,
};

enum class AbiExternalWrapperKind {
  CString,
  StringView,
  BytesView,
};

struct AbiFieldLayout {
  uint32_t offset = 0;
  AbiTypeInfo type;
};

struct AbiPaddingRange {
  uint32_t offset = 0;
  uint32_t size = 0;
  bool zero_initialized = true;
};

struct AbiContainmentField {
  uint32_t type_index = 0;
  bool indirect = false;
};

struct AbiDataDeclaration {
  std::vector<AbiTypeInfo> fields;
  uint32_t method_count = 0;
};

struct AbiAggregateLayout {
  uint32_t size = 0;
  uint32_t align = 1;
  bool contains_references = false;
  bool native_callable = true;
  bool external_ffi_callable = true;
  bool pass_by_value = true;
  uint64_t layout_hash = 0;
  std::vector<AbiFieldLayout> fields;
  std::vector<AbiPaddingRange> padding;
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

struct AbiPromiseId {
  uint32_t index = 0;
  uint32_t generation = 0;

  bool IsNull() const { return generation == 0; }
};

AbiTypeInfo GetPrimitiveAbiTypeInfo(Simple::Byte::TypeKind kind);
AbiTypeInfo GetEnumAbiTypeInfo(Simple::Byte::TypeKind underlying_kind);
AbiTypeInfo GetAggregateAbiTypeInfo(const AbiAggregateLayout& layout);
AbiTypeInfo GetExternalCAbiWrapperTypeInfo(AbiExternalWrapperKind kind);
AbiPassMode GetAbiParameterPassMode(const AbiTypeInfo& type);
AbiPassMode GetAbiReturnPassMode(const AbiTypeInfo& type);
AbiPassMode GetAbiAggregatePassMode(const AbiAggregateLayout& layout);
uint32_t AlignAbiOffset(uint32_t offset, uint32_t alignment);
bool IsReferenceAbiClass(AbiClass abi_class);
bool IsOpaqueVmReferenceType(Simple::Byte::TypeKind kind);
bool IsSmallAbiAggregate(uint32_t size, bool contains_references);
bool IsValidBorrowedStringView(const SimpleStringView& view);
bool IsValidBorrowedBytesView(const SimpleBytesView& view);
AbiVariantValue MakeAbiOptionNone();
AbiVariantValue MakeAbiOptionSome(uint64_t payload);
AbiVariantValue MakeAbiResultOk(uint64_t payload);
AbiVariantValue MakeAbiResultErr(uint64_t payload);
uint64_t PackAbiPromiseId(AbiPromiseId promise);
AbiPromiseId UnpackAbiPromiseId(uint64_t value);
bool IsAbiOptionSome(const AbiVariantValue& value);
bool IsAbiResultOk(const AbiVariantValue& value);
bool IsAbiResultErr(const AbiVariantValue& value);
uint64_t ComputeStableAggregateLayoutHash(const AbiAggregateLayout& layout);
AbiAggregateLayout ComputeStableAggregateLayout(const std::vector<AbiTypeInfo>& fields);
AbiAggregateLayout ComputeStableDataLayout(const AbiDataDeclaration& declaration);
bool ValidateNoRecursiveValueContainment(
    const std::vector<std::vector<AbiContainmentField>>& type_fields,
    std::string* error);
bool ValidateAbiCallableSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                  Simple::Byte::TypeKind result_type,
                                  bool external_ffi,
                                  std::string* error);
bool ValidateExternalCAbiSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                   Simple::Byte::TypeKind result_type,
                                   std::string* error);
bool ValidateExternalCAbiTypeInfos(const std::vector<AbiTypeInfo>& parameter_types,
                                   const AbiTypeInfo& result_type,
                                   std::string* error);

} // namespace Simple::VM::Runtime
