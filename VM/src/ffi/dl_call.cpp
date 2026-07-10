#include "ffi/dl_call.h"

#include <cstring>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#if !defined(_WIN32)
#include <ffi.h>
#endif

#include "runtime/abi.h"
#include "runtime/values.h"

namespace Simple::VM::Ffi {

using Simple::Byte::SbcModule;
using Simple::Byte::TypeKind;
using Slot = Simple::VM::Interpreter::Slot;
using Simple::VM::Runtime::BitsToF32;
using Simple::VM::Runtime::BitsToF64;
using Simple::VM::Runtime::F32ToBits;
using Simple::VM::Runtime::F64ToBits;
using Simple::VM::Runtime::PackF32Bits;
using Simple::VM::Runtime::PackF64Bits;
using Simple::VM::Runtime::PackI32;
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::UnpackI32;
using Simple::VM::Runtime::UnpackI64;
using Simple::VM::Runtime::UnpackRef;
using Simple::VM::Runtime::UnpackU32Bits;
using Simple::VM::Runtime::UnpackU64Bits;
constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

inline bool IsDlCallScalarKind(TypeKind kind, bool allow_void) {
  if (allow_void && kind == TypeKind::Unspecified) return true;
  switch (kind) {
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
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::String:
      return true;
    default:
      return false;
  }
}

template <typename T>
bool ConvertDlArg(Slot slot,
                  Heap& heap,
                  std::vector<std::string>& owned_strings,
                  T* out,
                  std::string* out_error) {
  if (!out) return false;
  if constexpr (std::is_same_v<T, int8_t>) {
    *out = static_cast<int8_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, int16_t>) {
    *out = static_cast<int16_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    *out = static_cast<int32_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    *out = static_cast<int64_t>(UnpackI64(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint8_t>) {
    *out = static_cast<uint8_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    *out = static_cast<uint16_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    *out = static_cast<uint32_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    *out = static_cast<uint64_t>(UnpackI64(slot));
    return true;
  } else if constexpr (std::is_same_v<T, float>) {
    *out = BitsToF32(UnpackU32Bits(slot));
    return true;
  } else if constexpr (std::is_same_v<T, double>) {
    *out = BitsToF64(UnpackU64Bits(slot));
    return true;
  } else if constexpr (std::is_same_v<T, bool>) {
    *out = (UnpackI32(slot) != 0);
    return true;
  } else if constexpr (std::is_same_v<T, const char*>) {
    uint32_t ref = UnpackRef(slot);
    if (ref == kNullRef) {
      *out = nullptr;
      return true;
    }
    HeapObject* obj = heap.Get(ref);
    if (!obj || obj->header.kind != ObjectKind::String) {
      if (out_error) *out_error = "System.dl.call string argument is not a string";
      return false;
    }
    owned_strings.push_back(U16ToAscii(ReadString(obj)));
    *out = owned_strings.back().c_str();
    return true;
  }
  if (out_error) *out_error = "System.dl.call unsupported argument type conversion";
  return false;
}

template <typename T>
bool PackDlReturn(T value, Heap& heap, Slot* out_ret, std::string* out_error) {
  if (!out_ret) return false;
  if constexpr (std::is_same_v<T, int8_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, int16_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    *out_ret = PackI32(value);
    return true;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    *out_ret = PackI64(value);
    return true;
  } else if constexpr (std::is_same_v<T, uint8_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    *out_ret = PackI64(static_cast<int64_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, float>) {
    *out_ret = PackF32Bits(F32ToBits(value));
    return true;
  } else if constexpr (std::is_same_v<T, double>) {
    *out_ret = PackF64Bits(F64ToBits(value));
    return true;
  } else if constexpr (std::is_same_v<T, bool>) {
    *out_ret = PackI32(value ? 1 : 0);
    return true;
  } else if constexpr (std::is_same_v<T, const char*>) {
    if (!value) {
      *out_ret = PackRef(kNullRef);
      return true;
    }
    uint32_t handle = CreateString(heap, AsciiToU16(value));
    *out_ret = PackRef(handle);
    return true;
  }
  if (out_error) *out_error = "System.dl.call unsupported return type conversion";
  return false;
}

template <typename Ret, typename... Args>
bool InvokeDlFunctionTyped(int64_t ptr_bits,
                           const std::vector<Slot>& args,
                           size_t arg_base,
                           Heap& heap,
                           Slot* out_ret,
                           std::string* out_error) {
  std::vector<std::string> owned_strings;
  owned_strings.reserve(sizeof...(Args));
  std::tuple<std::decay_t<Args>...> converted{};
  bool ok = true;
  size_t index = 0;
  auto convert_one = [&](auto& dst) {
    if (!ok) return;
    using ArgT = std::decay_t<decltype(dst)>;
    if (arg_base + index >= args.size()) {
      ok = false;
      if (out_error) *out_error = "System.dl.call arg index out of range";
      return;
    }
    if (!ConvertDlArg<ArgT>(args[arg_base + index], heap, owned_strings, &dst, out_error)) {
      ok = false;
      return;
    }
    ++index;
  };
  std::apply([&](auto&... vals) { (convert_one(vals), ...); }, converted);
  if (!ok) return false;
  using Fn = Ret (*)(Args...);
  Fn fn = reinterpret_cast<Fn>(ptr_bits);
  Ret value = std::apply([&](auto... vals) -> Ret { return fn(vals...); }, converted);
  return PackDlReturn<Ret>(value, heap, out_ret, out_error);
}

template <typename... Args>
bool InvokeDlFunctionVoidTyped(int64_t ptr_bits,
                               const std::vector<Slot>& args,
                               size_t arg_base,
                               Heap& heap,
                               std::string* out_error) {
  std::vector<std::string> owned_strings;
  owned_strings.reserve(sizeof...(Args));
  std::tuple<std::decay_t<Args>...> converted{};
  bool ok = true;
  size_t index = 0;
  auto convert_one = [&](auto& dst) {
    if (!ok) return;
    using ArgT = std::decay_t<decltype(dst)>;
    if (arg_base + index >= args.size()) {
      ok = false;
      if (out_error) *out_error = "System.dl.call arg index out of range";
      return;
    }
    if (!ConvertDlArg<ArgT>(args[arg_base + index], heap, owned_strings, &dst, out_error)) {
      ok = false;
      return;
    }
    ++index;
  };
  std::apply([&](auto&... vals) { (convert_one(vals), ...); }, converted);
  if (!ok) return false;
  using Fn = void (*)(Args...);
  Fn fn = reinterpret_cast<Fn>(ptr_bits);
  std::apply([&](auto... vals) { fn(vals...); }, converted);
  return true;
}

#define SIMPLE_DL_FOREACH_TYPE(X) \
  X(TypeKind::I8, int8_t)         \
  X(TypeKind::I16, int16_t)       \
  X(TypeKind::I32, int32_t)       \
  X(TypeKind::I64, int64_t)       \
  X(TypeKind::U8, uint8_t)        \
  X(TypeKind::U16, uint16_t)      \
  X(TypeKind::U32, uint32_t)      \
  X(TypeKind::U64, uint64_t)      \
  X(TypeKind::F32, float)         \
  X(TypeKind::F64, double)        \
  X(TypeKind::Bool, bool)         \
  X(TypeKind::Char, uint8_t)      \
  X(TypeKind::String, const char*)

template <typename Ret>
bool DispatchDlCall1(TypeKind arg0_kind,
                     int64_t ptr_bits,
                     const std::vector<Slot>& args,
                     size_t arg_base,
                     Heap& heap,
                     Slot* out_ret,
                     std::string* out_error) {
  switch (arg0_kind) {
#define SIMPLE_DL_CASE_ARG0(kind, cpp_type) \
    case kind:                              \
      return InvokeDlFunctionTyped<Ret, cpp_type>(ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG0)
#undef SIMPLE_DL_CASE_ARG0
    default:
      if (out_error) *out_error = "System.dl.call unsupported parameter type";
      return false;
  }
}

template <typename Arg0>
bool DispatchDlCall2Arg1Void(TypeKind arg1_kind,
                             int64_t ptr_bits,
                             const std::vector<Slot>& args,
                             size_t arg_base,
                             Heap& heap,
                             std::string* out_error) {
  switch (arg1_kind) {
#define SIMPLE_DL_CASE_ARG1_VOID(kind, cpp_type) \
    case kind:                                   \
      return InvokeDlFunctionVoidTyped<Arg0, cpp_type>(ptr_bits, args, arg_base, heap, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG1_VOID)
#undef SIMPLE_DL_CASE_ARG1_VOID
    default:
      if (out_error) *out_error = "System.dl.call unsupported parameter type";
      return false;
  }
}

template <typename Ret, typename Arg0>
bool DispatchDlCall2Arg1(TypeKind arg1_kind,
                         int64_t ptr_bits,
                         const std::vector<Slot>& args,
                         size_t arg_base,
                         Heap& heap,
                         Slot* out_ret,
                         std::string* out_error) {
  switch (arg1_kind) {
#define SIMPLE_DL_CASE_ARG1(kind, cpp_type) \
    case kind:                              \
      return InvokeDlFunctionTyped<Ret, Arg0, cpp_type>(ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG1)
#undef SIMPLE_DL_CASE_ARG1
    default:
      if (out_error) *out_error = "System.dl.call unsupported parameter type";
      return false;
  }
}

template <typename Ret>
bool DispatchDlCall2(TypeKind arg0_kind,
                     TypeKind arg1_kind,
                     int64_t ptr_bits,
                     const std::vector<Slot>& args,
                     size_t arg_base,
                     Heap& heap,
                     Slot* out_ret,
                     std::string* out_error) {
  switch (arg0_kind) {
#define SIMPLE_DL_CASE_ARG0_2(kind, cpp_type) \
    case kind:                                \
      return DispatchDlCall2Arg1<Ret, cpp_type>(arg1_kind, ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG0_2)
#undef SIMPLE_DL_CASE_ARG0_2
    default:
      if (out_error) *out_error = "System.dl.call unsupported parameter type";
      return false;
  }
}

#if !defined(_WIN32)
struct DlOwnedFfiType {
  ffi_type type{};
  std::vector<ffi_type*> elements;
};

struct DlStructMeta {
  ffi_type* ffi = nullptr;
  std::vector<uint32_t> field_type_ids;
  std::vector<uint32_t> vm_offsets;
  std::vector<size_t> ffi_offsets;
  bool offsets_ready = false;
};

struct DlAbiCache {
  std::unordered_map<uint32_t, ffi_type*> ffi_by_type;
  std::unordered_map<uint32_t, DlStructMeta> struct_meta;
  std::vector<std::unique_ptr<DlOwnedFfiType>> owned_types;
};

size_t AlignSize(size_t value, size_t align) {
  if (align <= 1) return value;
  size_t mask = align - 1;
  return (value + mask) & ~mask;
}

bool IsStructTypeId(const SbcModule& module, uint32_t type_id) {
  if (type_id >= module.types.size()) return false;
  const auto& row = module.types[type_id];
  return static_cast<TypeKind>(row.kind) == TypeKind::Unspecified && row.field_count > 0;
}

ffi_type* PrimitiveFfiType(TypeKind kind) {
  switch (kind) {
    case TypeKind::I8: return &ffi_type_sint8;
    case TypeKind::I16: return &ffi_type_sint16;
    case TypeKind::I32: return &ffi_type_sint32;
    case TypeKind::I64: return &ffi_type_sint64;
    case TypeKind::U8:
    case TypeKind::Bool:
    case TypeKind::Char:
      return &ffi_type_uint8;
    case TypeKind::U16: return &ffi_type_uint16;
    case TypeKind::U32: return &ffi_type_uint32;
    case TypeKind::U64: return &ffi_type_uint64;
    case TypeKind::F32: return &ffi_type_float;
    case TypeKind::F64: return &ffi_type_double;
    case TypeKind::String:
    case TypeKind::Ref:
      return &ffi_type_pointer;
    default:
      return nullptr;
  }
}

bool BuildExternalDlAbiTypeInfo(const SbcModule& module,
                                uint32_t type_id,
                                std::unordered_set<uint32_t>& visiting,
                                Simple::VM::Runtime::AbiTypeInfo* out,
                                std::string* out_error) {
  if (!out) return false;
  if (type_id >= module.types.size()) {
    if (out_error) *out_error = "System.dl.call type id out of range";
    return false;
  }
  const auto& row = module.types[type_id];
  const TypeKind kind = static_cast<TypeKind>(row.kind);
  if (kind == TypeKind::String) {
    *out = Simple::VM::Runtime::GetExternalCAbiWrapperTypeInfo(
        Simple::VM::Runtime::AbiExternalWrapperKind::CString);
    return true;
  }
  if (kind != TypeKind::Unspecified || row.field_count == 0) {
    *out = Simple::VM::Runtime::GetPrimitiveAbiTypeInfo(kind);
    return true;
  }
  if (!visiting.insert(type_id).second) {
    if (out_error) *out_error = "System.dl.call recursive struct ABI is unsupported";
    return false;
  }
  if (row.field_start + row.field_count > module.fields.size()) {
    if (out_error) *out_error = "System.dl.call struct field range out of bounds";
    visiting.erase(type_id);
    return false;
  }
  std::vector<Simple::VM::Runtime::AbiTypeInfo> fields;
  fields.reserve(row.field_count);
  for (uint32_t i = 0; i < row.field_count; ++i) {
    Simple::VM::Runtime::AbiTypeInfo field;
    if (!BuildExternalDlAbiTypeInfo(module,
                                    module.fields[row.field_start + i].type_id,
                                    visiting,
                                    &field,
                                    out_error)) {
      visiting.erase(type_id);
      return false;
    }
    fields.push_back(field);
  }
  visiting.erase(type_id);
  *out = Simple::VM::Runtime::GetAggregateAbiTypeInfo(
      Simple::VM::Runtime::ComputeStableAggregateLayout(fields));
  return true;
}

bool BuildExternalDlAbiTypeInfo(const SbcModule& module,
                                uint32_t type_id,
                                Simple::VM::Runtime::AbiTypeInfo* out,
                                std::string* out_error) {
  std::unordered_set<uint32_t> visiting;
  return BuildExternalDlAbiTypeInfo(module, type_id, visiting, out, out_error);
}

ffi_type* BuildDlFfiType(const SbcModule& module,
                         uint32_t type_id,
                         DlAbiCache& cache,
                         std::unordered_set<uint32_t>* visiting,
                         std::string* out_error) {
  auto cached = cache.ffi_by_type.find(type_id);
  if (cached != cache.ffi_by_type.end()) return cached->second;
  if (type_id >= module.types.size()) {
    if (out_error) *out_error = "System.dl.call type id out of range";
    return nullptr;
  }
  const auto& row = module.types[type_id];
  const TypeKind kind = static_cast<TypeKind>(row.kind);
  if (kind != TypeKind::Unspecified || row.field_count == 0) {
    ffi_type* primitive = PrimitiveFfiType(kind);
    if (!primitive) {
      if (out_error) *out_error = "System.dl.call unsupported ABI type";
      return nullptr;
    }
    cache.ffi_by_type[type_id] = primitive;
    return primitive;
  }
  if (!visiting || !visiting->insert(type_id).second) {
    if (out_error) *out_error = "System.dl.call recursive struct ABI is unsupported";
    return nullptr;
  }
  if (row.field_start + row.field_count > module.fields.size()) {
    if (out_error) *out_error = "System.dl.call struct field range out of bounds";
    visiting->erase(type_id);
    return nullptr;
  }
  auto owned = std::make_unique<DlOwnedFfiType>();
  owned->type.type = FFI_TYPE_STRUCT;
  owned->elements.reserve(static_cast<size_t>(row.field_count) + 1u);
  DlStructMeta meta;
  meta.ffi = &owned->type;
  meta.field_type_ids.reserve(row.field_count);
  meta.vm_offsets.reserve(row.field_count);
  for (uint32_t i = 0; i < row.field_count; ++i) {
    const auto& field = module.fields[row.field_start + i];
    ffi_type* field_ffi = BuildDlFfiType(module, field.type_id, cache, visiting, out_error);
    if (!field_ffi) {
      visiting->erase(type_id);
      return nullptr;
    }
    owned->elements.push_back(field_ffi);
    meta.field_type_ids.push_back(field.type_id);
    meta.vm_offsets.push_back(field.offset);
  }
  owned->elements.push_back(nullptr);
  owned->type.elements = owned->elements.data();
  ffi_type* out = &owned->type;
  cache.owned_types.push_back(std::move(owned));
  cache.ffi_by_type[type_id] = out;
  cache.struct_meta[type_id] = std::move(meta);
  visiting->erase(type_id);
  return out;
}

ffi_type* BuildDlFfiType(const SbcModule& module,
                         uint32_t type_id,
                         DlAbiCache& cache,
                         std::string* out_error) {
  std::unordered_set<uint32_t> visiting;
  return BuildDlFfiType(module, type_id, cache, &visiting, out_error);
}

bool PrepareStructOffsets(const SbcModule& module,
                          uint32_t type_id,
                          DlAbiCache& cache,
                          std::string* out_error) {
  auto meta_it = cache.struct_meta.find(type_id);
  if (meta_it == cache.struct_meta.end()) return true;
  DlStructMeta& meta = meta_it->second;
  if (meta.offsets_ready) return true;
  const auto& row = module.types[type_id];
  size_t offset = 0;
  size_t max_align = 1;
  meta.ffi_offsets.assign(row.field_count, 0);
  for (size_t i = 0; i < row.field_count; ++i) {
    uint32_t field_type_id = meta.field_type_ids[i];
    if (IsStructTypeId(module, field_type_id) &&
        !PrepareStructOffsets(module, field_type_id, cache, out_error)) {
      return false;
    }
    ffi_type* field_ffi = meta.ffi->elements[i];
    if (!field_ffi || field_ffi->size == 0 || field_ffi->alignment == 0) {
      if (out_error) *out_error = "System.dl.call struct field ABI is incomplete";
      return false;
    }
    size_t align = static_cast<size_t>(field_ffi->alignment);
    offset = AlignSize(offset, align);
    meta.ffi_offsets[i] = offset;
    offset += field_ffi->size;
    if (align > max_align) max_align = align;
  }
  size_t computed = AlignSize(offset, max_align);
  if (meta.ffi->size != 0 && computed != meta.ffi->size) {
    if (out_error) *out_error = "System.dl.call struct ABI size mismatch";
    return false;
  }
  meta.offsets_ready = true;
  return true;
}

bool ReadVmPayloadScalar(const std::vector<uint8_t>& payload,
                         size_t offset,
                         TypeKind kind,
                         Heap& heap,
                         std::vector<std::string>& owned_strings,
                         void* out_value,
                         std::string* out_error) {
  auto require = [&](size_t n) -> bool {
    if (offset + n > payload.size()) {
      if (out_error) *out_error = "System.dl.call struct payload out of bounds";
      return false;
    }
    return true;
  };
  switch (kind) {
    case TypeKind::I8: {
      if (!require(1)) return false;
      *static_cast<int8_t*>(out_value) = static_cast<int8_t>(payload[offset]);
      return true;
    }
    case TypeKind::I16: {
      if (!require(2)) return false;
      int16_t v = 0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<int16_t*>(out_value) = v;
      return true;
    }
    case TypeKind::I32: {
      if (!require(4)) return false;
      int32_t v = 0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<int32_t*>(out_value) = v;
      return true;
    }
    case TypeKind::I64: {
      if (!require(8)) return false;
      int64_t v = 0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<int64_t*>(out_value) = v;
      return true;
    }
    case TypeKind::U8:
    case TypeKind::Bool:
    case TypeKind::Char: {
      if (!require(1)) return false;
      *static_cast<uint8_t*>(out_value) = payload[offset];
      return true;
    }
    case TypeKind::U16: {
      if (!require(2)) return false;
      uint16_t v = 0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<uint16_t*>(out_value) = v;
      return true;
    }
    case TypeKind::U32: {
      if (!require(4)) return false;
      uint32_t v = 0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<uint32_t*>(out_value) = v;
      return true;
    }
    case TypeKind::U64: {
      if (!require(8)) return false;
      uint64_t v = 0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<uint64_t*>(out_value) = v;
      return true;
    }
    case TypeKind::F32: {
      if (!require(4)) return false;
      float v = 0.0f;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<float*>(out_value) = v;
      return true;
    }
    case TypeKind::F64: {
      if (!require(8)) return false;
      double v = 0.0;
      std::memcpy(&v, payload.data() + offset, sizeof(v));
      *static_cast<double*>(out_value) = v;
      return true;
    }
    case TypeKind::String: {
      if (!require(4)) return false;
      uint32_t ref = 0;
      std::memcpy(&ref, payload.data() + offset, sizeof(ref));
      if (ref == kNullRef) {
        *static_cast<const char**>(out_value) = nullptr;
        return true;
      }
      HeapObject* obj = heap.Get(ref);
      if (!obj || obj->header.kind != ObjectKind::String) {
        if (out_error) *out_error = "System.dl.call struct string field is not a string";
        return false;
      }
      owned_strings.push_back(U16ToAscii(ReadString(obj)));
      *static_cast<const char**>(out_value) = owned_strings.back().c_str();
      return true;
    }
    default:
      if (out_error) *out_error = "System.dl.call unsupported struct field type";
      return false;
  }
}

bool WriteVmPayloadScalar(std::vector<uint8_t>* payload,
                          size_t offset,
                          TypeKind kind,
                          const void* value,
                          Heap& heap,
                          std::string* out_error) {
  if (!payload) return false;
  auto require = [&](size_t n) -> bool {
    if (offset + n > payload->size()) {
      if (out_error) *out_error = "System.dl.call struct payload out of bounds";
      return false;
    }
    return true;
  };
  switch (kind) {
    case TypeKind::I8: {
      if (!require(1)) return false;
      (*payload)[offset] = static_cast<uint8_t>(*static_cast<const int8_t*>(value));
      return true;
    }
    case TypeKind::I16: {
      if (!require(2)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(int16_t));
      return true;
    }
    case TypeKind::I32: {
      if (!require(4)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(int32_t));
      return true;
    }
    case TypeKind::I64: {
      if (!require(8)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(int64_t));
      return true;
    }
    case TypeKind::U8:
    case TypeKind::Bool:
    case TypeKind::Char: {
      if (!require(1)) return false;
      (*payload)[offset] = *static_cast<const uint8_t*>(value);
      return true;
    }
    case TypeKind::U16: {
      if (!require(2)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(uint16_t));
      return true;
    }
    case TypeKind::U32: {
      if (!require(4)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(uint32_t));
      return true;
    }
    case TypeKind::U64: {
      if (!require(8)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(uint64_t));
      return true;
    }
    case TypeKind::F32: {
      if (!require(4)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(float));
      return true;
    }
    case TypeKind::F64: {
      if (!require(8)) return false;
      std::memcpy(payload->data() + offset, value, sizeof(double));
      return true;
    }
    case TypeKind::String: {
      if (!require(4)) return false;
      const char* text = *static_cast<const char* const*>(value);
      uint32_t ref = kNullRef;
      if (text) ref = CreateString(heap, AsciiToU16(text));
      std::memcpy(payload->data() + offset, &ref, sizeof(ref));
      return true;
    }
    default:
      if (out_error) *out_error = "System.dl.call unsupported struct field type";
      return false;
  }
}

bool MarshalVmArtifactToFfi(const SbcModule& module,
                            uint32_t type_id,
                            uint32_t handle,
                            DlAbiCache& cache,
                            Heap& heap,
                            std::vector<std::string>& owned_strings,
                            void* out_value,
                            std::string* out_error);

bool MarshalFfiToVmArtifact(const SbcModule& module,
                            uint32_t type_id,
                            const void* value,
                            DlAbiCache& cache,
                            Heap& heap,
                            uint32_t* out_handle,
                            std::string* out_error);

bool MarshalVmArtifactToFfi(const SbcModule& module,
                            uint32_t type_id,
                            uint32_t handle,
                            DlAbiCache& cache,
                            Heap& heap,
                            std::vector<std::string>& owned_strings,
                            void* out_value,
                            std::string* out_error) {
  if (!IsStructTypeId(module, type_id)) {
    if (out_error) *out_error = "System.dl.call expected struct type";
    return false;
  }
  if (handle == kNullRef) {
    if (out_error) *out_error = "System.dl.call null struct argument";
    return false;
  }
  HeapObject* obj = heap.Get(handle);
  if (!obj || obj->header.kind != ObjectKind::Artifact || obj->header.type_id != type_id) {
    if (out_error) *out_error = "System.dl.call struct argument type mismatch";
    return false;
  }
  auto meta_it = cache.struct_meta.find(type_id);
  if (meta_it == cache.struct_meta.end()) {
    if (out_error) *out_error = "System.dl.call struct metadata missing";
    return false;
  }
  DlStructMeta& meta = meta_it->second;
  if (!meta.offsets_ready && !PrepareStructOffsets(module, type_id, cache, out_error)) return false;
  const auto& row = module.types[type_id];
  for (size_t i = 0; i < row.field_count; ++i) {
    uint32_t field_type_id = meta.field_type_ids[i];
    size_t vm_offset = static_cast<size_t>(meta.vm_offsets[i]);
    uint8_t* dst = static_cast<uint8_t*>(out_value) + meta.ffi_offsets[i];
    if (IsStructTypeId(module, field_type_id)) {
      if (vm_offset + 4 > obj->payload.size()) {
        if (out_error) *out_error = "System.dl.call nested struct field out of bounds";
        return false;
      }
      uint32_t nested = 0;
      std::memcpy(&nested, obj->payload.data() + vm_offset, sizeof(nested));
      if (!MarshalVmArtifactToFfi(module, field_type_id, nested, cache, heap, owned_strings, dst, out_error)) {
        return false;
      }
      continue;
    }
    TypeKind field_kind = static_cast<TypeKind>(module.types[field_type_id].kind);
    if (!ReadVmPayloadScalar(obj->payload, vm_offset, field_kind, heap, owned_strings, dst, out_error)) {
      return false;
    }
  }
  return true;
}

bool MarshalFfiToVmArtifact(const SbcModule& module,
                            uint32_t type_id,
                            const void* value,
                            DlAbiCache& cache,
                            Heap& heap,
                            uint32_t* out_handle,
                            std::string* out_error) {
  if (!out_handle) return false;
  if (!IsStructTypeId(module, type_id)) {
    if (out_error) *out_error = "System.dl.call expected struct return type";
    return false;
  }
  auto meta_it = cache.struct_meta.find(type_id);
  if (meta_it == cache.struct_meta.end()) {
    if (out_error) *out_error = "System.dl.call struct metadata missing";
    return false;
  }
  DlStructMeta& meta = meta_it->second;
  if (!meta.offsets_ready && !PrepareStructOffsets(module, type_id, cache, out_error)) return false;
  uint32_t handle = heap.Allocate(ObjectKind::Artifact, type_id, module.types[type_id].size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) {
    if (out_error) *out_error = "System.dl.call artifact allocation failed";
    return false;
  }
  const auto& row = module.types[type_id];
  for (size_t i = 0; i < row.field_count; ++i) {
    uint32_t field_type_id = meta.field_type_ids[i];
    size_t vm_offset = static_cast<size_t>(meta.vm_offsets[i]);
    const uint8_t* src = static_cast<const uint8_t*>(value) + meta.ffi_offsets[i];
    if (IsStructTypeId(module, field_type_id)) {
      uint32_t nested = kNullRef;
      if (!MarshalFfiToVmArtifact(module, field_type_id, src, cache, heap, &nested, out_error)) return false;
      if (vm_offset + 4 > obj->payload.size()) {
        if (out_error) *out_error = "System.dl.call nested struct field out of bounds";
        return false;
      }
      std::memcpy(obj->payload.data() + vm_offset, &nested, sizeof(nested));
      continue;
    }
    TypeKind field_kind = static_cast<TypeKind>(module.types[field_type_id].kind);
    if (!WriteVmPayloadScalar(&obj->payload, vm_offset, field_kind, src, heap, out_error)) return false;
  }
  *out_handle = handle;
  return true;
}

bool FillScalarArgStorage(const SbcModule& module,
                          uint32_t type_id,
                          Slot slot,
                          Heap& heap,
                          std::vector<std::string>& owned_strings,
                          void* out_value,
                          std::string* out_error) {
  if (type_id >= module.types.size()) {
    if (out_error) *out_error = "System.dl.call type id out of range";
    return false;
  }
  TypeKind kind = static_cast<TypeKind>(module.types[type_id].kind);
  switch (kind) {
    case TypeKind::I8: return ConvertDlArg<int8_t>(slot, heap, owned_strings, static_cast<int8_t*>(out_value), out_error);
    case TypeKind::I16: return ConvertDlArg<int16_t>(slot, heap, owned_strings, static_cast<int16_t*>(out_value), out_error);
    case TypeKind::I32: return ConvertDlArg<int32_t>(slot, heap, owned_strings, static_cast<int32_t*>(out_value), out_error);
    case TypeKind::I64: return ConvertDlArg<int64_t>(slot, heap, owned_strings, static_cast<int64_t*>(out_value), out_error);
    case TypeKind::U8:
    case TypeKind::Bool:
    case TypeKind::Char:
      return ConvertDlArg<uint8_t>(slot, heap, owned_strings, static_cast<uint8_t*>(out_value), out_error);
    case TypeKind::U16:
      return ConvertDlArg<uint16_t>(slot, heap, owned_strings, static_cast<uint16_t*>(out_value), out_error);
    case TypeKind::U32:
      return ConvertDlArg<uint32_t>(slot, heap, owned_strings, static_cast<uint32_t*>(out_value), out_error);
    case TypeKind::U64:
    case TypeKind::Ref:
      return ConvertDlArg<uint64_t>(slot, heap, owned_strings, static_cast<uint64_t*>(out_value), out_error);
    case TypeKind::F32:
      return ConvertDlArg<float>(slot, heap, owned_strings, static_cast<float*>(out_value), out_error);
    case TypeKind::F64:
      return ConvertDlArg<double>(slot, heap, owned_strings, static_cast<double*>(out_value), out_error);
    case TypeKind::String:
      return ConvertDlArg<const char*>(slot, heap, owned_strings, static_cast<const char**>(out_value), out_error);
    default:
      if (out_error) *out_error = "System.dl.call unsupported parameter type";
      return false;
  }
}

bool ValidateDynamicDlCallSignature(const SbcModule& module,
                                    uint32_t ret_type_id,
                                    bool has_ret,
                                    const std::vector<uint32_t>& arg_type_ids,
                                    std::string* out_error) {
  std::vector<Simple::VM::Runtime::AbiTypeInfo> parameter_abi;
  parameter_abi.reserve(arg_type_ids.size());
  for (uint32_t type_id : arg_type_ids) {
    Simple::VM::Runtime::AbiTypeInfo type;
    if (!BuildExternalDlAbiTypeInfo(module, type_id, &type, out_error)) return false;
    parameter_abi.push_back(type);
  }
  Simple::VM::Runtime::AbiTypeInfo result_abi =
      Simple::VM::Runtime::GetPrimitiveAbiTypeInfo(TypeKind::Unspecified);
  if (has_ret && !BuildExternalDlAbiTypeInfo(module, ret_type_id, &result_abi, out_error)) {
    return false;
  }
  return Simple::VM::Runtime::ValidateExternalCAbiTypeInfos(parameter_abi, result_abi, out_error);
}

bool ValidateDynamicDlFunctionSignature(const SbcModule& module,
                                        uint32_t ret_type_id,
                                        bool has_ret,
                                        const std::vector<uint32_t>& param_type_ids,
                                        std::string* out_error) {
  if (param_type_ids.empty()) {
    if (out_error) *out_error = "System.dl.call missing function pointer parameter";
    return false;
  }
  const uint32_t ptr_type_id = param_type_ids.front();
  if (ptr_type_id >= module.types.size()) {
    if (out_error) *out_error = "System.dl.call function pointer type id out of range";
    return false;
  }
  const auto ptr_kind = static_cast<TypeKind>(module.types[ptr_type_id].kind);
  if (ptr_kind != TypeKind::I64 && ptr_kind != TypeKind::U64) {
    if (out_error) *out_error = "System.dl.call function pointer must be i64/u64";
    return false;
  }
  std::vector<uint32_t> arg_type_ids(param_type_ids.begin() + 1, param_type_ids.end());
  return ValidateDynamicDlCallSignature(module, ret_type_id, has_ret, arg_type_ids, out_error);
}

bool DispatchDynamicDlCall(int64_t ptr_bits,
                           const SbcModule& module,
                           uint32_t ret_type_id,
                           bool has_ret,
                           const std::vector<uint32_t>& arg_type_ids,
                           const std::vector<Slot>& args,
                           size_t arg_base,
                           Heap& heap,
                           Slot* out_ret,
                           std::string* out_error) {
  if (!ValidateDynamicDlCallSignature(module, ret_type_id, has_ret, arg_type_ids, out_error)) return false;

  DlAbiCache cache;
  std::vector<std::string> owned_strings;
  std::vector<ffi_type*> ffi_arg_types(arg_type_ids.size(), nullptr);
  for (size_t i = 0; i < arg_type_ids.size(); ++i) {
    ffi_arg_types[i] = BuildDlFfiType(module, arg_type_ids[i], cache, out_error);
    if (!ffi_arg_types[i]) return false;
  }

  ffi_type* ffi_ret_type = &ffi_type_void;
  if (has_ret) {
    ffi_ret_type = BuildDlFfiType(module, ret_type_id, cache, out_error);
    if (!ffi_ret_type) return false;
  }

  ffi_cif cif;
  if (ffi_prep_cif(&cif,
                   FFI_DEFAULT_ABI,
                   static_cast<unsigned int>(arg_type_ids.size()),
                   ffi_ret_type,
                   ffi_arg_types.data()) != FFI_OK) {
    if (out_error) *out_error = "System.dl.call ffi_prep_cif failed";
    return false;
  }
  for (uint32_t type_id : arg_type_ids) {
    if (IsStructTypeId(module, type_id) && !PrepareStructOffsets(module, type_id, cache, out_error)) {
      return false;
    }
  }
  if (has_ret && IsStructTypeId(module, ret_type_id) &&
      !PrepareStructOffsets(module, ret_type_id, cache, out_error)) {
    return false;
  }
  std::vector<std::vector<uint8_t>> arg_storage(arg_type_ids.size());
  std::vector<void*> ffi_arg_values(arg_type_ids.size(), nullptr);
  for (size_t i = 0; i < arg_type_ids.size(); ++i) {
    if (arg_base + i >= args.size()) {
      if (out_error) *out_error = "System.dl.call arg index out of range";
      return false;
    }
    ffi_type* field_ffi = ffi_arg_types[i];
    size_t size = (field_ffi && field_ffi->size > 0) ? field_ffi->size : sizeof(uint64_t);
    arg_storage[i].assign(size, 0);
    uint32_t type_id = arg_type_ids[i];
    if (IsStructTypeId(module, type_id)) {
      uint32_t ref = UnpackRef(args[arg_base + i]);
      if (!MarshalVmArtifactToFfi(module,
                                  type_id,
                                  ref,
                                  cache,
                                  heap,
                                  owned_strings,
                                  arg_storage[i].data(),
                                  out_error)) {
        return false;
      }
    } else if (!FillScalarArgStorage(module,
                                     type_id,
                                     args[arg_base + i],
                                     heap,
                                     owned_strings,
                                     arg_storage[i].data(),
                                     out_error)) {
      return false;
    }
    ffi_arg_values[i] = arg_storage[i].data();
  }
  std::vector<uint8_t> ret_storage;
  void* ret_ptr = nullptr;
  if (has_ret) {
    size_t size = (ffi_ret_type && ffi_ret_type->size > 0) ? ffi_ret_type->size : sizeof(uint64_t);
    ret_storage.assign(size, 0);
    ret_ptr = ret_storage.data();
  }

  void (*fn)() = reinterpret_cast<void (*)()>(static_cast<uintptr_t>(ptr_bits));
  ffi_call(&cif,
           FFI_FN(fn),
           ret_ptr,
           ffi_arg_values.data());

  if (!has_ret) return true;
  if (!out_ret) {
    if (out_error) *out_error = "System.dl.call missing return slot";
    return false;
  }
  if (IsStructTypeId(module, ret_type_id)) {
    uint32_t handle = kNullRef;
    if (!MarshalFfiToVmArtifact(module, ret_type_id, ret_storage.data(), cache, heap, &handle, out_error)) {
      return false;
    }
    *out_ret = PackRef(handle);
    return true;
  }
  TypeKind ret_kind = static_cast<TypeKind>(module.types[ret_type_id].kind);
  switch (ret_kind) {
    case TypeKind::I8: return PackDlReturn<int8_t>(*reinterpret_cast<int8_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::I16: return PackDlReturn<int16_t>(*reinterpret_cast<int16_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::I32: return PackDlReturn<int32_t>(*reinterpret_cast<int32_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::I64: return PackDlReturn<int64_t>(*reinterpret_cast<int64_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::U8: return PackDlReturn<uint8_t>(*reinterpret_cast<uint8_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::U16: return PackDlReturn<uint16_t>(*reinterpret_cast<uint16_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::U32: return PackDlReturn<uint32_t>(*reinterpret_cast<uint32_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::U64:
    case TypeKind::Ref:
      return PackDlReturn<uint64_t>(*reinterpret_cast<uint64_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::F32: return PackDlReturn<float>(*reinterpret_cast<float*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::F64: return PackDlReturn<double>(*reinterpret_cast<double*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::Bool: return PackDlReturn<bool>((*reinterpret_cast<uint8_t*>(ret_storage.data())) != 0, heap, out_ret, out_error);
    case TypeKind::Char: return PackDlReturn<uint8_t>(*reinterpret_cast<uint8_t*>(ret_storage.data()), heap, out_ret, out_error);
    case TypeKind::String: return PackDlReturn<const char*>(*reinterpret_cast<const char**>(ret_storage.data()), heap, out_ret, out_error);
    default:
      if (out_error) *out_error = "System.dl.call unsupported return type";
      return false;
  }
}
#else
bool DispatchDynamicDlCall(int64_t /*ptr_bits*/,
                           const SbcModule& /*module*/,
                           uint32_t /*ret_type_id*/,
                           bool /*has_ret*/,
                           const std::vector<uint32_t>& /*arg_type_ids*/,
                           const std::vector<Slot>& /*args*/,
                           size_t /*arg_base*/,
                           Heap& /*heap*/,
                           Slot* /*out_ret*/,
                           std::string* out_error) {
  if (out_error) *out_error = "System.dl.call is unsupported on windows";
  return false;
}
#endif

#undef SIMPLE_DL_FOREACH_TYPE


} // namespace Simple::VM::Ffi
