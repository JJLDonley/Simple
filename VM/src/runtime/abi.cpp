#include "runtime/abi.h"

#include <algorithm>

namespace Simple::VM::Runtime {
namespace {

constexpr uint32_t kDefaultAggregateAlign = 1;
constexpr uint32_t kMaxStableAggregateAlign = 8;

uint32_t ClampStableAggregateAlign(uint32_t alignment) {
  if (alignment == 0) return kDefaultAggregateAlign;
  return std::min(alignment, kMaxStableAggregateAlign);
}

} // namespace

AbiTypeInfo GetPrimitiveAbiTypeInfo(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::Void:
    case TypeKind::Unspecified:
      return AbiTypeInfo{AbiClass::Void, 0, 1, true, true};
    case TypeKind::Bool:
    case TypeKind::I8:
    case TypeKind::U8:
      return AbiTypeInfo{AbiClass::Scalar, 1, 1, true, true};
    case TypeKind::I16:
    case TypeKind::U16:
      return AbiTypeInfo{AbiClass::Scalar, 2, 2, true, true};
    case TypeKind::I32:
    case TypeKind::U32:
    case TypeKind::Char:
      return AbiTypeInfo{AbiClass::Scalar, 4, 4, true, true};
    case TypeKind::I64:
    case TypeKind::U64:
      return AbiTypeInfo{AbiClass::Scalar, 8, 8, true, true};
    case TypeKind::F32:
      return AbiTypeInfo{AbiClass::Float, 4, 4, true, true};
    case TypeKind::F64:
      return AbiTypeInfo{AbiClass::Float, 8, 8, true, true};
    case TypeKind::Ref:
    case TypeKind::String:
    case TypeKind::Array:
    case TypeKind::List:
    case TypeKind::Function:
      return AbiTypeInfo{AbiClass::Ref, 8, 8, true, false};
    case TypeKind::Ptr:
      return AbiTypeInfo{AbiClass::Scalar, 8, 8, true, true};
    case TypeKind::Result:
    case TypeKind::Option:
      return AbiTypeInfo{AbiClass::Variant, 16, 8, true, false};
    case TypeKind::Vector:
      return AbiTypeInfo{AbiClass::Aggregate, 16, 16, true, false};
    case TypeKind::Never:
      return AbiTypeInfo{AbiClass::Void, 0, 1, false, false};
    default:
      return AbiTypeInfo{};
  }
}

uint32_t AlignAbiOffset(uint32_t offset, uint32_t alignment) {
  if (alignment <= 1) return offset;
  const uint32_t remainder = offset % alignment;
  return remainder == 0 ? offset : offset + (alignment - remainder);
}

bool IsReferenceAbiClass(AbiClass abi_class) {
  return abi_class == AbiClass::Ref || abi_class == AbiClass::Handle ||
         abi_class == AbiClass::Promise || abi_class == AbiClass::Opaque;
}

bool IsSmallAbiAggregate(uint32_t size, bool contains_references) {
  return !contains_references && size <= 16;
}

AbiAggregateLayout ComputeStableAggregateLayout(const std::vector<AbiTypeInfo>& fields) {
  AbiAggregateLayout layout;
  layout.fields.reserve(fields.size());

  uint32_t offset = 0;
  for (const AbiTypeInfo& field : fields) {
    if (field.abi_class == AbiClass::Invalid || field.align == 0) {
      layout.native_callable = false;
      layout.external_ffi_callable = false;
      layout.pass_by_value = false;
      layout.fields.push_back(AbiFieldLayout{offset, field});
      continue;
    }

    const uint32_t field_align = ClampStableAggregateAlign(field.align);
    offset = AlignAbiOffset(offset, field_align);
    layout.fields.push_back(AbiFieldLayout{offset, field});
    offset += field.size;
    layout.align = std::max(layout.align, field_align);
    layout.contains_references = layout.contains_references || IsReferenceAbiClass(field.abi_class) ||
                                 field.abi_class == AbiClass::Variant;
    layout.native_callable = layout.native_callable && field.native_callable;
    layout.external_ffi_callable = layout.external_ffi_callable && field.external_ffi_callable;
  }

  layout.size = AlignAbiOffset(offset, layout.align);
  layout.pass_by_value = layout.native_callable && IsSmallAbiAggregate(layout.size, layout.contains_references);
  return layout;
}

} // namespace Simple::VM::Runtime
