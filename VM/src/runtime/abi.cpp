#include "runtime/abi.h"

#include <algorithm>
#include <string>

namespace Simple::VM::Runtime {
namespace {

constexpr uint32_t kDefaultAggregateAlign = 1;
constexpr uint32_t kMaxStableAggregateAlign = 8;
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint32_t ClampStableAggregateAlign(uint32_t alignment) {
  if (alignment == 0) return kDefaultAggregateAlign;
  return std::min(alignment, kMaxStableAggregateAlign);
}

void HashByte(uint64_t* hash, uint8_t value) {
  *hash ^= value;
  *hash *= kFnvPrime;
}

void HashU32(uint64_t* hash, uint32_t value) {
  for (uint32_t i = 0; i < 4; ++i) {
    HashByte(hash, static_cast<uint8_t>((value >> (i * 8u)) & 0xffu));
  }
}

void HashBool(uint64_t* hash, bool value) {
  HashByte(hash, value ? 1u : 0u);
}

void HashTypeInfo(uint64_t* hash, const AbiTypeInfo& type) {
  HashU32(hash, static_cast<uint32_t>(type.abi_class));
  HashU32(hash, type.size);
  HashU32(hash, type.align);
  HashBool(hash, type.native_callable);
  HashBool(hash, type.external_ffi_callable);
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

AbiPassMode GetAbiParameterPassMode(const AbiTypeInfo& type) {
  if (!type.native_callable || type.abi_class == AbiClass::Invalid) return AbiPassMode::Invalid;
  switch (type.abi_class) {
    case AbiClass::Void:
      return AbiPassMode::Void;
    case AbiClass::Scalar:
    case AbiClass::Float:
    case AbiClass::Ref:
    case AbiClass::Handle:
    case AbiClass::Variant:
    case AbiClass::Promise:
      return AbiPassMode::Direct;
    case AbiClass::Aggregate:
      return type.size <= 16 ? AbiPassMode::Direct : AbiPassMode::Indirect;
    case AbiClass::Opaque:
      return AbiPassMode::Indirect;
    case AbiClass::Invalid:
      return AbiPassMode::Invalid;
  }
  return AbiPassMode::Invalid;
}

AbiPassMode GetAbiReturnPassMode(const AbiTypeInfo& type) {
  return GetAbiParameterPassMode(type);
}

AbiPassMode GetAbiAggregatePassMode(const AbiAggregateLayout& layout) {
  if (!layout.native_callable) return AbiPassMode::Invalid;
  return layout.pass_by_value ? AbiPassMode::Direct : AbiPassMode::Indirect;
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

bool IsOpaqueVmReferenceType(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::Ref:
    case TypeKind::String:
    case TypeKind::Array:
    case TypeKind::List:
    case TypeKind::Function:
      return true;
    default:
      return false;
  }
}

bool IsSmallAbiAggregate(uint32_t size, bool contains_references) {
  return !contains_references && size <= 16;
}

bool IsValidBorrowedStringView(const SimpleStringView& view) {
  return (view.data != nullptr || view.size == 0) && view.encoding == AbiStringEncoding::Utf8;
}

bool IsValidBorrowedBytesView(const SimpleBytesView& view) {
  return view.data != nullptr || view.size == 0;
}

AbiVariantValue MakeAbiOptionNone() {
  return AbiVariantValue{AbiVariantTag::None, 0, 0};
}

AbiVariantValue MakeAbiOptionSome(uint64_t payload) {
  return AbiVariantValue{AbiVariantTag::Some, 0, payload};
}

AbiVariantValue MakeAbiResultOk(uint64_t payload) {
  return AbiVariantValue{AbiVariantTag::Ok, 0, payload};
}

AbiVariantValue MakeAbiResultErr(uint64_t payload) {
  return AbiVariantValue{AbiVariantTag::Err, 0, payload};
}

uint64_t PackAbiPromiseId(AbiPromiseId promise) {
  return (static_cast<uint64_t>(promise.generation) << 32u) | promise.index;
}

AbiPromiseId UnpackAbiPromiseId(uint64_t value) {
  AbiPromiseId promise;
  promise.index = static_cast<uint32_t>(value & 0xffffffffu);
  promise.generation = static_cast<uint32_t>(value >> 32u);
  return promise;
}

bool IsAbiOptionSome(const AbiVariantValue& value) {
  return value.tag == AbiVariantTag::Some;
}

bool IsAbiResultOk(const AbiVariantValue& value) {
  return value.tag == AbiVariantTag::Ok;
}

bool IsAbiResultErr(const AbiVariantValue& value) {
  return value.tag == AbiVariantTag::Err;
}

const char* TypeKindAbiName(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::Unspecified: return "unspecified";
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::I8: return "i8";
    case TypeKind::U8: return "u8";
    case TypeKind::I16: return "i16";
    case TypeKind::U16: return "u16";
    case TypeKind::I32: return "i32";
    case TypeKind::U32: return "u32";
    case TypeKind::I64: return "i64";
    case TypeKind::U64: return "u64";
    case TypeKind::F32: return "f32";
    case TypeKind::F64: return "f64";
    case TypeKind::Char: return "char";
    case TypeKind::String: return "string";
    case TypeKind::Array: return "array";
    case TypeKind::List: return "list";
    case TypeKind::Ref: return "ref";
    case TypeKind::Ptr: return "ptr";
    case TypeKind::Function: return "function";
    case TypeKind::Result: return "result";
    case TypeKind::Option: return "option";
    case TypeKind::Vector: return "vector";
    case TypeKind::Never: return "never";
    default: return "unknown";
  }
}

bool IsVoidLikeResult(Simple::Byte::TypeKind kind) {
  return kind == Simple::Byte::TypeKind::Void || kind == Simple::Byte::TypeKind::Unspecified;
}

uint64_t ComputeStableAggregateLayoutHash(const AbiAggregateLayout& layout) {
  uint64_t hash = kFnvOffset;
  HashU32(&hash, layout.size);
  HashU32(&hash, layout.align);
  HashBool(&hash, layout.contains_references);
  HashBool(&hash, layout.native_callable);
  HashBool(&hash, layout.external_ffi_callable);
  HashBool(&hash, layout.pass_by_value);
  HashU32(&hash, static_cast<uint32_t>(layout.fields.size()));
  for (const AbiFieldLayout& field : layout.fields) {
    HashU32(&hash, field.offset);
    HashTypeInfo(&hash, field.type);
  }
  HashU32(&hash, static_cast<uint32_t>(layout.padding.size()));
  for (const AbiPaddingRange& range : layout.padding) {
    HashU32(&hash, range.offset);
    HashU32(&hash, range.size);
    HashBool(&hash, range.zero_initialized);
  }
  return hash;
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
    const uint32_t aligned_offset = AlignAbiOffset(offset, field_align);
    if (aligned_offset > offset) {
      layout.padding.push_back(AbiPaddingRange{offset, aligned_offset - offset, true});
    }
    offset = aligned_offset;
    layout.fields.push_back(AbiFieldLayout{offset, field});
    offset += field.size;
    layout.align = std::max(layout.align, field_align);
    layout.contains_references = layout.contains_references || IsReferenceAbiClass(field.abi_class) ||
                                 field.abi_class == AbiClass::Variant;
    layout.native_callable = layout.native_callable && field.native_callable;
    layout.external_ffi_callable = layout.external_ffi_callable && field.external_ffi_callable;
  }

  layout.size = AlignAbiOffset(offset, layout.align);
  if (layout.size > offset) {
    layout.padding.push_back(AbiPaddingRange{offset, layout.size - offset, true});
  }
  layout.pass_by_value = layout.native_callable && IsSmallAbiAggregate(layout.size, layout.contains_references);
  layout.layout_hash = ComputeStableAggregateLayoutHash(layout);
  return layout;
}

bool ValidateAbiCallableSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                  Simple::Byte::TypeKind result_type,
                                  bool external_ffi,
                                  std::string* error) {
  for (size_t i = 0; i < parameter_types.size(); ++i) {
    const Simple::Byte::TypeKind kind = parameter_types[i];
    if (kind == Simple::Byte::TypeKind::Void || kind == Simple::Byte::TypeKind::Unspecified ||
        kind == Simple::Byte::TypeKind::Never) {
      if (error) *error = "parameter " + std::to_string(i) + " has non-value ABI type " + TypeKindAbiName(kind);
      return false;
    }
    const AbiTypeInfo info = GetPrimitiveAbiTypeInfo(kind);
    if (info.abi_class == AbiClass::Invalid || !info.native_callable ||
        (external_ffi && !info.external_ffi_callable)) {
      if (error) *error = "parameter " + std::to_string(i) + " is not callable by ABI as " + TypeKindAbiName(kind);
      return false;
    }
  }

  if (result_type == Simple::Byte::TypeKind::Never) {
    if (error) *error = "result has non-returning ABI type never";
    return false;
  }
  const AbiTypeInfo result_info = GetPrimitiveAbiTypeInfo(result_type);
  if (!IsVoidLikeResult(result_type) &&
      (result_info.abi_class == AbiClass::Invalid || !result_info.native_callable ||
       (external_ffi && !result_info.external_ffi_callable))) {
    if (error) *error = std::string("result is not callable by ABI as ") + TypeKindAbiName(result_type);
    return false;
  }
  return true;
}

bool ValidateExternalCAbiSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                   Simple::Byte::TypeKind result_type,
                                   std::string* error) {
  return ValidateAbiCallableSignature(parameter_types, result_type, true, error);
}

} // namespace Simple::VM::Runtime
