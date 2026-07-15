#include "runtime/abi.h"

#include <algorithm>
#include <string>
#include <unordered_set>

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
      return AbiTypeInfo{AbiClass::Scalar, 1, 1, true, true};
    case TypeKind::I8:
    case TypeKind::U8:
      return AbiTypeInfo{AbiClass::Scalar, 1, 1, true, true};
    case TypeKind::I16:
    case TypeKind::U16:
      return AbiTypeInfo{AbiClass::Scalar, 2, 2, true, true};
    case TypeKind::I32:
    case TypeKind::U32:
      return AbiTypeInfo{AbiClass::Scalar, 4, 4, true, true};
    case TypeKind::Char:
      return AbiTypeInfo{AbiClass::Scalar, 4, 4, true, false};
    case TypeKind::I64:
    case TypeKind::U64:
      return AbiTypeInfo{AbiClass::Scalar, 8, 8, true, true};
    case TypeKind::ISize:
    case TypeKind::USize:
      return AbiTypeInfo{AbiClass::Scalar, static_cast<uint32_t>(sizeof(void*)),
                         static_cast<uint32_t>(alignof(void*)), true, true};
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
      return AbiTypeInfo{AbiClass::Scalar, static_cast<uint32_t>(sizeof(void*)),
                         static_cast<uint32_t>(alignof(void*)), true, false};
    case TypeKind::Result:
    case TypeKind::Optional:
      return AbiTypeInfo{AbiClass::Variant, 16, 8, true, false};
    case TypeKind::Vector:
      return AbiTypeInfo{AbiClass::Aggregate, 16, 16, true, false};
    case TypeKind::Never:
      return AbiTypeInfo{AbiClass::Void, 0, 1, false, false};
    default:
      return AbiTypeInfo{};
  }
}

AbiTypeInfo GetAggregateAbiTypeInfo(const AbiAggregateLayout& layout) {
  return AbiTypeInfo{AbiClass::Aggregate,
                     layout.size,
                     std::min(layout.align, kMaxStableAggregateAlign),
                     layout.native_callable,
                     layout.external_ffi_callable};
}

AbiTypeInfo GetSbcTypeAbiTypeInfo(const Simple::Byte::TypeRow& row) {
  if (Simple::Byte::IsOpaqueHandleType(row)) {
    return AbiTypeInfo{AbiClass::Handle, 8, 8, true, false};
  }
  if (Simple::Byte::IsManagedClassType(row)) {
    return AbiTypeInfo{AbiClass::Ref, 8, 8, true, false};
  }
  AbiTypeInfo info =
      GetPrimitiveAbiTypeInfo(static_cast<Simple::Byte::TypeKind>(row.kind));
  if (static_cast<Simple::Byte::TypeKind>(row.kind) ==
      Simple::Byte::TypeKind::Ptr) {
    info.pointer_value = true;
  }
  if (info.pointer_value &&
      (row.flags & Simple::Byte::kTypeFlagPointerExternal) != 0u) {
    info.external_ffi_callable = true;
    info.external_pointer = true;
    info.pointer_nullable =
        (row.flags & Simple::Byte::kTypeFlagPointerNullable) != 0u;
    info.function_pointer =
        (row.flags & Simple::Byte::kTypeFlagPointerFunction) != 0u;
    const Simple::Byte::ExternalPointerFlow flow =
        Simple::Byte::GetExternalPointerFlow(row);
    if ((row.flags & Simple::Byte::kTypeFlagPointerReadOnly) != 0u) {
      info.pointer_access = AbiPointerAccess::ReadOnly;
    } else if (flow == Simple::Byte::ExternalPointerFlow::Output) {
      info.pointer_access = AbiPointerAccess::WriteOnly;
    } else {
      info.pointer_access = AbiPointerAccess::ReadWrite;
    }
    switch (flow) {
      case Simple::Byte::ExternalPointerFlow::Input:
        info.pointer_flow = AbiPointerFlow::Input;
        break;
      case Simple::Byte::ExternalPointerFlow::InOut:
        info.pointer_flow = AbiPointerFlow::InOut;
        break;
      case Simple::Byte::ExternalPointerFlow::Output:
        info.pointer_flow = AbiPointerFlow::Output;
        break;
      case Simple::Byte::ExternalPointerFlow::Result:
        info.pointer_flow = AbiPointerFlow::Result;
        break;
      case Simple::Byte::ExternalPointerFlow::None:
        info.pointer_flow = AbiPointerFlow::None;
        break;
    }
    info.pointer_ownership = AbiPointerOwnership::Borrowed;
  }
  return info;
}

bool GetSbcModuleTypeAbiTypeInfoImpl(const Simple::Byte::SbcModule& module,
                                     uint32_t type_id,
                                     std::unordered_set<uint32_t>& visiting,
                                     AbiTypeInfo* out,
                                     std::string* error) {
  if (!out) return false;
  if (type_id >= module.types.size()) {
    if (error) *error = "SBC type id out of range";
    return false;
  }
  const Simple::Byte::TypeRow& row = module.types[type_id];
  if (Simple::Byte::IsOpaqueHandleType(row)) {
    *out = GetSbcTypeAbiTypeInfo(row);
    return true;
  }
  if (Simple::Byte::IsManagedClassType(row)) {
    *out = AbiTypeInfo{AbiClass::Ref, 8, 8, true, false};
    return true;
  }
  const auto kind = static_cast<Simple::Byte::TypeKind>(row.kind);
  if (kind != Simple::Byte::TypeKind::Unspecified) {
    *out = GetSbcTypeAbiTypeInfo(row);
    return out->abi_class != AbiClass::Invalid;
  }
  if (!Simple::Byte::IsStableStructType(row)) {
    if (error) *error = "SBC aggregate type is not a stable struct";
    return false;
  }
  if (!visiting.insert(type_id).second) {
    if (error) *error = "recursive stable struct ABI type";
    return false;
  }
  if (row.field_start + row.field_count > module.fields.size()) {
    if (error) *error = "stable struct field range out of bounds";
    visiting.erase(type_id);
    return false;
  }
  std::vector<AbiTypeInfo> fields;
  fields.reserve(row.field_count);
  for (uint32_t i = 0; i < row.field_count; ++i) {
    AbiTypeInfo field;
    if (!GetSbcModuleTypeAbiTypeInfoImpl(module,
                                         module.fields[row.field_start + i].type_id,
                                         visiting,
                                         &field,
                                         error)) {
      visiting.erase(type_id);
      return false;
    }
    fields.push_back(field);
  }
  visiting.erase(type_id);
  *out = GetAggregateAbiTypeInfo(ComputeStableAggregateLayout(fields));
  return true;
}

bool GetSbcModuleTypeAbiTypeInfo(const Simple::Byte::SbcModule& module,
                                 uint32_t type_id,
                                 AbiTypeInfo* out,
                                 std::string* error) {
  std::unordered_set<uint32_t> visiting;
  return GetSbcModuleTypeAbiTypeInfoImpl(module, type_id, visiting, out, error);
}

AbiTypeInfo GetEnumAbiTypeInfo(Simple::Byte::TypeKind underlying_kind) {
  using Simple::Byte::TypeKind;
  if (underlying_kind == TypeKind::Unspecified) underlying_kind = TypeKind::I32;
  switch (underlying_kind) {
    case TypeKind::I8:
    case TypeKind::U8:
    case TypeKind::I16:
    case TypeKind::U16:
    case TypeKind::I32:
    case TypeKind::U32:
    case TypeKind::I64:
    case TypeKind::U64:
      return GetPrimitiveAbiTypeInfo(underlying_kind);
    default:
      return AbiTypeInfo{};
  }
}

AbiTypeInfo GetExternalCAbiWrapperTypeInfo(AbiExternalWrapperKind kind) {
  switch (kind) {
    case AbiExternalWrapperKind::NullTerminatedU8:
      return AbiTypeInfo{AbiClass::Scalar, 8, 8, true, true};
    case AbiExternalWrapperKind::StringView:
    case AbiExternalWrapperKind::BytesView:
      return AbiTypeInfo{AbiClass::Aggregate, 16, 8, true, true};
  }
  return AbiTypeInfo{};
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

bool IsValidAbiScalarValue(Simple::Byte::TypeKind kind, uint64_t value) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::Bool:
      return value == 0 || value == 1;
    case TypeKind::Char: {
      const uint32_t codepoint = static_cast<uint32_t>(value);
      return value == codepoint && codepoint <= 0x10FFFFu &&
             (codepoint < 0xD800u || codepoint > 0xDFFFu);
    }
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
    case TypeKind::F32:
    case TypeKind::F64:
    case TypeKind::Ptr:
      return true;
    default:
      return false;
  }
}

bool IsValidBorrowedStringView(const SimpleStringView& view) {
  return (view.data != nullptr || view.size == 0) && view.encoding == AbiStringEncoding::Utf8;
}

bool IsValidBorrowedBytesView(const SimpleBytesView& view) {
  return view.data != nullptr || view.size == 0;
}

AbiVariantValue MakeAbiOptionalAbsent() {
  return AbiVariantValue{AbiVariantTag::Absent, 0, 0};
}

AbiVariantValue MakeAbiOptionalPresent(uint64_t payload) {
  return AbiVariantValue{AbiVariantTag::Present, 0, payload};
}

AbiVariantValue MakeAbiResultValue(uint64_t payload) {
  return AbiVariantValue{AbiVariantTag::Value, 0, payload};
}

AbiVariantValue MakeAbiResultError(uint64_t payload) {
  return AbiVariantValue{AbiVariantTag::Error, 0, payload};
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

bool IsAbiOptionalPresent(const AbiVariantValue& value) {
  return value.tag == AbiVariantTag::Present;
}

bool IsAbiResultValue(const AbiVariantValue& value) {
  return value.tag == AbiVariantTag::Value;
}

bool IsAbiResultError(const AbiVariantValue& value) {
  return value.tag == AbiVariantTag::Error;
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
    case TypeKind::Optional: return "optional";
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

AbiFixedArrayLayout ComputeStableFixedArrayLayout(const AbiTypeInfo& element_type,
                                                  uint32_t length) {
  AbiFixedArrayLayout layout;
  layout.element_type = element_type;
  layout.length = length;
  layout.align = ClampStableAggregateAlign(element_type.align);
  layout.element_stride = AlignAbiOffset(element_type.size, layout.align);
  layout.contains_references = IsReferenceAbiClass(element_type.abi_class);
  layout.native_callable = element_type.native_callable;
  layout.external_ffi_callable = element_type.external_ffi_callable && !layout.contains_references;
  if (layout.native_callable && length > 0 && layout.element_stride != 0 &&
      length <= UINT32_MAX / layout.element_stride) {
    layout.size = layout.element_stride * length;
  } else if (length == 0) {
    layout.size = 0;
  } else {
    layout.native_callable = false;
    layout.external_ffi_callable = false;
  }
  layout.pass_by_value = layout.native_callable && IsSmallAbiAggregate(layout.size, layout.contains_references);
  return layout;
}

bool ValidateNoRecursiveValueContainment(
    const std::vector<std::vector<AbiContainmentField>>& type_fields,
    std::string* error) {
  enum class VisitState : uint8_t { Unvisited, Visiting, Done };
  std::vector<VisitState> states(type_fields.size(), VisitState::Unvisited);

  auto dfs = [&](auto&& self, uint32_t type_index) -> bool {
    if (type_index >= type_fields.size()) {
      if (error) *error = "containment field type index out of range";
      return false;
    }
    if (states[type_index] == VisitState::Visiting) {
      if (error) *error = "recursive value containment at type " + std::to_string(type_index);
      return false;
    }
    if (states[type_index] == VisitState::Done) return true;

    states[type_index] = VisitState::Visiting;
    for (const AbiContainmentField& field : type_fields[type_index]) {
      if (field.indirect) continue;
      if (field.type_index >= type_fields.size()) {
        if (error) *error = "containment field type index out of range";
        return false;
      }
      if (!self(self, field.type_index)) return false;
    }
    states[type_index] = VisitState::Done;
    return true;
  };

  for (uint32_t i = 0; i < type_fields.size(); ++i) {
    if (states[i] == VisitState::Unvisited && !dfs(dfs, i)) return false;
  }
  if (error) error->clear();
  return true;
}

bool ValidateAbiCallableSignature(const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                  Simple::Byte::TypeKind result_type,
                                  std::string* error) {
  for (size_t i = 0; i < parameter_types.size(); ++i) {
    const Simple::Byte::TypeKind kind = parameter_types[i];
    if (kind == Simple::Byte::TypeKind::Void || kind == Simple::Byte::TypeKind::Unspecified ||
        kind == Simple::Byte::TypeKind::Never) {
      if (error) *error = "parameter " + std::to_string(i) + " has non-value ABI type " + TypeKindAbiName(kind);
      return false;
    }
    const AbiTypeInfo info = GetPrimitiveAbiTypeInfo(kind);
    if (info.abi_class == AbiClass::Invalid || !info.native_callable) {
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
      (result_info.abi_class == AbiClass::Invalid || !result_info.native_callable)) {
    if (error) *error = std::string("result is not callable by ABI as ") + TypeKindAbiName(result_type);
    return false;
  }
  return true;
}

bool ValidateExternalCAbiTypeInfos(const std::vector<AbiTypeInfo>& parameter_types,
                                   const AbiTypeInfo& result_type,
                                   std::string* error) {
  for (size_t i = 0; i < parameter_types.size(); ++i) {
    const AbiTypeInfo& type = parameter_types[i];
    if (type.pointer_value &&
        (!type.external_pointer ||
         type.pointer_ownership != AbiPointerOwnership::Borrowed ||
         type.pointer_access == AbiPointerAccess::None)) {
      if (error) {
        *error = "parameter " + std::to_string(i) +
                 " lacks external pointer lifetime metadata";
      }
      return false;
    }
    if (type.abi_class == AbiClass::Void || type.abi_class == AbiClass::Invalid ||
        !type.external_ffi_callable) {
      if (error) *error = "parameter " + std::to_string(i) + " is not external-C ABI callable";
      return false;
    }
  }
  if (result_type.pointer_value &&
      (!result_type.external_pointer ||
       result_type.pointer_ownership != AbiPointerOwnership::Borrowed ||
       result_type.pointer_access == AbiPointerAccess::None)) {
    if (error) *error = "result lacks external pointer lifetime metadata";
    return false;
  }
  if (result_type.abi_class != AbiClass::Void &&
      (result_type.abi_class == AbiClass::Invalid || !result_type.external_ffi_callable)) {
    if (error) *error = "result is not external-C ABI callable";
    return false;
  }
  return true;
}

} // namespace Simple::VM::Runtime
