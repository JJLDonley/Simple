#include "runtime/abi.h"

namespace Simple::VM::Runtime {

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

bool IsSmallAbiAggregate(uint32_t size, bool contains_references) {
  return !contains_references && size <= 16;
}

} // namespace Simple::VM::Runtime
