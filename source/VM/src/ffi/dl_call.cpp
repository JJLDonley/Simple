#include "ffi/dl_call.h"

#include <cstring>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <ffi.h>

#include "runtime/abi.h"
#include "runtime/values.h"

namespace Simple::VM::Ffi {

using Simple::Byte::SbcModule;
using Simple::Byte::TypeKind;
using Slot = Simple::VM::Interpreter::Slot;
using Simple::VM::Runtime::PackF32;
using Simple::VM::Runtime::PackF64;
using Simple::VM::Runtime::PackI32;
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::UnpackI32;
using Simple::VM::Runtime::UnpackF32;
using Simple::VM::Runtime::UnpackF64;
using Simple::VM::Runtime::UnpackI64;
using Simple::VM::Runtime::UnpackRef;
constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

template <typename T>
bool ConvertDlArg(Slot slot, T* out, std::string* out_error) {
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
    *out = UnpackF32(slot);
    return true;
  } else if constexpr (std::is_same_v<T, double>) {
    *out = UnpackF64(slot);
    return true;
  }
  if (out_error) *out_error = "System.FFI.call unsupported argument type conversion";
  return false;
}

template <typename T>
bool PackDlReturn(T value, Slot* out_ret, std::string* out_error) {
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
    *out_ret = PackF32(value);
    return true;
  } else if constexpr (std::is_same_v<T, double>) {
    *out_ret = PackF64(value);
    return true;
  }
  if (out_error) *out_error = "System.FFI.call unsupported return type conversion";
  return false;
}

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
    case TypeKind::ISize:
      return sizeof(intptr_t) == 8 ? &ffi_type_sint64 : &ffi_type_sint32;
    case TypeKind::U8:
      return &ffi_type_uint8;
    case TypeKind::U16: return &ffi_type_uint16;
    case TypeKind::U32: return &ffi_type_uint32;
    case TypeKind::U64: return &ffi_type_uint64;
    case TypeKind::USize:
      return sizeof(uintptr_t) == 8 ? &ffi_type_uint64 : &ffi_type_uint32;
    case TypeKind::F32: return &ffi_type_float;
    case TypeKind::F64: return &ffi_type_double;
    case TypeKind::Ptr:
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
    if (out_error) *out_error = "System.FFI.call type id out of range";
    return false;
  }
  const auto& row = module.types[type_id];
  const TypeKind kind = static_cast<TypeKind>(row.kind);
  if (kind != TypeKind::Unspecified || row.field_count == 0) {
    *out = Simple::VM::Runtime::GetPrimitiveAbiTypeInfo(kind);
    return true;
  }
  if (!visiting.insert(type_id).second) {
    if (out_error) *out_error = "System.FFI.call recursive struct ABI is unsupported";
    return false;
  }
  if (row.field_start + row.field_count > module.fields.size()) {
    if (out_error) *out_error = "System.FFI.call struct field range out of bounds";
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
    if (out_error) *out_error = "System.FFI.call type id out of range";
    return nullptr;
  }
  const auto& row = module.types[type_id];
  const TypeKind kind = static_cast<TypeKind>(row.kind);
  if (kind != TypeKind::Unspecified || row.field_count == 0) {
    ffi_type* primitive = PrimitiveFfiType(kind);
    if (!primitive) {
      if (out_error) *out_error = "System.FFI.call unsupported ABI type";
      return nullptr;
    }
    cache.ffi_by_type[type_id] = primitive;
    return primitive;
  }
  if (!visiting || !visiting->insert(type_id).second) {
    if (out_error) *out_error = "System.FFI.call recursive struct ABI is unsupported";
    return nullptr;
  }
  if (row.field_start + row.field_count > module.fields.size()) {
    if (out_error) *out_error = "System.FFI.call struct field range out of bounds";
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
      if (out_error) *out_error = "System.FFI.call struct field ABI is incomplete";
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
    if (out_error) *out_error = "System.FFI.call struct ABI size mismatch";
    return false;
  }
  meta.offsets_ready = true;
  return true;
}

bool ReadVmPayloadScalar(const std::vector<uint8_t>& payload,
                         size_t offset,
                         TypeKind kind,
                         void* out_value,
                         std::string* out_error) {
  auto require = [&](size_t n) -> bool {
    if (offset + n > payload.size()) {
      if (out_error) *out_error = "System.FFI.call struct payload out of bounds";
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
    case TypeKind::ISize: {
      if (!require(8)) return false;
      int64_t value = 0;
      std::memcpy(&value, payload.data() + offset, sizeof(value));
      const intptr_t narrowed = static_cast<intptr_t>(value);
      if (static_cast<int64_t>(narrowed) != value) {
        if (out_error) *out_error = "System.FFI.call struct isize field does not fit host ABI";
        return false;
      }
      *static_cast<intptr_t*>(out_value) = narrowed;
      return true;
    }
    case TypeKind::U8: {
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
    case TypeKind::USize: {
      if (!require(8)) return false;
      uint64_t value = 0;
      std::memcpy(&value, payload.data() + offset, sizeof(value));
      const uintptr_t narrowed = static_cast<uintptr_t>(value);
      if (static_cast<uint64_t>(narrowed) != value) {
        if (out_error) *out_error = "System.FFI.call struct usize field does not fit host ABI";
        return false;
      }
      *static_cast<uintptr_t*>(out_value) = narrowed;
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
    default:
      if (out_error) *out_error = "System.FFI.call unsupported struct field type";
      return false;
  }
}

bool WriteVmPayloadScalar(std::vector<uint8_t>* payload,
                          size_t offset,
                          TypeKind kind,
                          const void* value,
                          std::string* out_error) {
  if (!payload) return false;
  auto require = [&](size_t n) -> bool {
    if (offset + n > payload->size()) {
      if (out_error) *out_error = "System.FFI.call struct payload out of bounds";
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
    case TypeKind::ISize: {
      if (!require(8)) return false;
      const int64_t expanded = static_cast<int64_t>(*static_cast<const intptr_t*>(value));
      std::memcpy(payload->data() + offset, &expanded, sizeof(expanded));
      return true;
    }
    case TypeKind::U8: {
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
    case TypeKind::USize: {
      if (!require(8)) return false;
      const uint64_t expanded = static_cast<uint64_t>(*static_cast<const uintptr_t*>(value));
      std::memcpy(payload->data() + offset, &expanded, sizeof(expanded));
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
    default:
      if (out_error) *out_error = "System.FFI.call unsupported struct field type";
      return false;
  }
}

bool MarshalVmArtifactToFfi(const SbcModule& module,
                            uint32_t type_id,
                            uint32_t handle,
                            DlAbiCache& cache,
                            Heap& heap,
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
                            void* out_value,
                            std::string* out_error) {
  if (!IsStructTypeId(module, type_id)) {
    if (out_error) *out_error = "System.FFI.call expected struct type";
    return false;
  }
  if (handle == kNullRef) {
    if (out_error) *out_error = "System.FFI.call null struct argument";
    return false;
  }
  HeapObject* obj = heap.Get(handle);
  if (!obj || obj->header.kind != ObjectKind::Artifact || obj->header.type_id != type_id) {
    if (out_error) *out_error = "System.FFI.call struct argument type mismatch";
    return false;
  }
  auto meta_it = cache.struct_meta.find(type_id);
  if (meta_it == cache.struct_meta.end()) {
    if (out_error) *out_error = "System.FFI.call struct metadata missing";
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
        if (out_error) *out_error = "System.FFI.call nested struct field out of bounds";
        return false;
      }
      uint32_t nested = 0;
      std::memcpy(&nested, obj->payload.data() + vm_offset, sizeof(nested));
      if (!MarshalVmArtifactToFfi(module, field_type_id, nested, cache, heap, dst, out_error)) {
        return false;
      }
      continue;
    }
    TypeKind field_kind = static_cast<TypeKind>(module.types[field_type_id].kind);
    if (!ReadVmPayloadScalar(obj->payload, vm_offset, field_kind, dst, out_error)) {
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
    if (out_error) *out_error = "System.FFI.call expected struct return type";
    return false;
  }
  auto meta_it = cache.struct_meta.find(type_id);
  if (meta_it == cache.struct_meta.end()) {
    if (out_error) *out_error = "System.FFI.call struct metadata missing";
    return false;
  }
  DlStructMeta& meta = meta_it->second;
  if (!meta.offsets_ready && !PrepareStructOffsets(module, type_id, cache, out_error)) return false;
  uint32_t handle = heap.Allocate(ObjectKind::Artifact, type_id, module.types[type_id].size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) {
    if (out_error) *out_error = "System.FFI.call artifact allocation failed";
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
        if (out_error) *out_error = "System.FFI.call nested struct field out of bounds";
        return false;
      }
      std::memcpy(obj->payload.data() + vm_offset, &nested, sizeof(nested));
      continue;
    }
    TypeKind field_kind = static_cast<TypeKind>(module.types[field_type_id].kind);
    if (!WriteVmPayloadScalar(&obj->payload, vm_offset, field_kind, src, out_error)) return false;
  }
  *out_handle = handle;
  return true;
}

bool FillScalarArgStorage(const SbcModule& module,
                          uint32_t type_id,
                          Slot slot,
                          void* out_value,
                          std::string* out_error) {
  if (type_id >= module.types.size()) {
    if (out_error) *out_error = "System.FFI.call type id out of range";
    return false;
  }
  TypeKind kind = static_cast<TypeKind>(module.types[type_id].kind);
  switch (kind) {
    case TypeKind::I8: return ConvertDlArg<int8_t>(slot, static_cast<int8_t*>(out_value), out_error);
    case TypeKind::I16: return ConvertDlArg<int16_t>(slot, static_cast<int16_t*>(out_value), out_error);
    case TypeKind::I32: return ConvertDlArg<int32_t>(slot, static_cast<int32_t*>(out_value), out_error);
    case TypeKind::I64: return ConvertDlArg<int64_t>(slot, static_cast<int64_t*>(out_value), out_error);
    case TypeKind::U8:
      return ConvertDlArg<uint8_t>(slot, static_cast<uint8_t*>(out_value), out_error);
    case TypeKind::U16:
      return ConvertDlArg<uint16_t>(slot, static_cast<uint16_t*>(out_value), out_error);
    case TypeKind::U32:
      return ConvertDlArg<uint32_t>(slot, static_cast<uint32_t*>(out_value), out_error);
    case TypeKind::U64:
      return ConvertDlArg<uint64_t>(slot, static_cast<uint64_t*>(out_value), out_error);
    case TypeKind::ISize: {
      const int64_t value = UnpackI64(slot);
      const intptr_t narrowed = static_cast<intptr_t>(value);
      if (static_cast<int64_t>(narrowed) != value) {
        if (out_error) *out_error = "System.FFI.call isize argument does not fit host ABI";
        return false;
      }
      *static_cast<intptr_t*>(out_value) = narrowed;
      return true;
    }
    case TypeKind::USize: {
      const uint64_t value = static_cast<uint64_t>(slot);
      const uintptr_t narrowed = static_cast<uintptr_t>(value);
      if (static_cast<uint64_t>(narrowed) != value) {
        if (out_error) *out_error = "System.FFI.call usize argument does not fit host ABI";
        return false;
      }
      *static_cast<uintptr_t*>(out_value) = narrowed;
      return true;
    }
    case TypeKind::Ptr: {
      const uintptr_t bits = static_cast<uintptr_t>(slot);
      if (static_cast<Slot>(bits) != slot) {
        if (out_error) *out_error = "System.FFI.call pointer does not fit host ABI";
        return false;
      }
      *static_cast<void**>(out_value) = reinterpret_cast<void*>(bits);
      return true;
    }
    case TypeKind::F32:
      return ConvertDlArg<float>(slot, static_cast<float*>(out_value), out_error);
    case TypeKind::F64:
      return ConvertDlArg<double>(slot, static_cast<double*>(out_value), out_error);
    default:
      if (out_error) *out_error = "System.FFI.call unsupported parameter type";
      return false;
  }
}

bool IsDlScalarParamMarshalSupported(TypeKind kind) {
  switch (kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
    case TypeKind::ISize:
    case TypeKind::USize:
    case TypeKind::F32:
    case TypeKind::F64:
    case TypeKind::Ptr:
      return true;
    default:
      return false;
  }
}

bool IsDlScalarReturnMarshalSupported(TypeKind kind) {
  return IsDlScalarParamMarshalSupported(kind);
}

bool ValidateDlVmMarshalType(const SbcModule& module,
                             uint32_t type_id,
                             bool is_return,
                             std::unordered_set<uint32_t>& visiting,
                             bool* may_allocate,
                             bool* needs_roots,
                             std::string* out_error) {
  if (type_id >= module.types.size()) {
    if (out_error) *out_error = "System.FFI.call type id out of range";
    return false;
  }
  const auto& row = module.types[type_id];
  const TypeKind kind = static_cast<TypeKind>(row.kind);
  if (IsStructTypeId(module, type_id)) {
    if (!visiting.insert(type_id).second) {
      if (out_error) *out_error = "System.FFI.call recursive struct marshal is unsupported";
      return false;
    }
    if (needs_roots) *needs_roots = true;
    if (is_return && may_allocate) *may_allocate = true;
    if (row.field_start + row.field_count > module.fields.size()) {
      if (out_error) *out_error = "System.FFI.call struct field range out of bounds";
      visiting.erase(type_id);
      return false;
    }
    for (uint32_t i = 0; i < row.field_count; ++i) {
      const uint32_t field_type_id = module.fields[row.field_start + i].type_id;
      if (!ValidateDlVmMarshalType(module, field_type_id, is_return, visiting, may_allocate, needs_roots, out_error)) {
        visiting.erase(type_id);
        return false;
      }
    }
    visiting.erase(type_id);
    return true;
  }
  const bool ok = is_return ? IsDlScalarReturnMarshalSupported(kind) : IsDlScalarParamMarshalSupported(kind);
  if (!ok && out_error) *out_error = "System.FFI.call unsupported VM marshal type";
  return ok;
}

bool ValidateDlVmMarshalSignature(const SbcModule& module,
                                  uint32_t ret_type_id,
                                  bool has_ret,
                                  const std::vector<uint32_t>& arg_type_ids,
                                  bool* may_allocate,
                                  bool* needs_roots,
                                  std::string* out_error) {
  std::unordered_set<uint32_t> visiting;
  for (uint32_t type_id : arg_type_ids) {
    if (!ValidateDlVmMarshalType(module, type_id, false, visiting, may_allocate, needs_roots, out_error)) return false;
  }
  if (has_ret && !ValidateDlVmMarshalType(module, ret_type_id, true, visiting, may_allocate, needs_roots, out_error)) {
    return false;
  }
  return true;
}

bool IsDlJitLoopScalarType(TypeKind kind) {
  switch (kind) {
    case TypeKind::Unspecified:
    case TypeKind::Void:
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
    case TypeKind::ISize:
    case TypeKind::USize:
    case TypeKind::F32:
    case TypeKind::F64:
      return true;
    default:
      return false;
  }
}

bool IsDlJitLoopSafeTypeId(const SbcModule& module, uint32_t type_id) {
  if (type_id == 0xFFFFFFFFu) return true;
  if (type_id >= module.types.size()) return false;
  if (IsStructTypeId(module, type_id)) return false;
  const auto kind = static_cast<TypeKind>(module.types[type_id].kind);
  return IsDlJitLoopScalarType(kind);
}

bool ValidateDlJitLoopSignature(const SbcModule& module,
                                uint32_t ret_type_id,
                                bool has_ret,
                                const std::vector<uint32_t>& arg_type_ids,
                                std::string* out_error) {
  if (has_ret && !IsDlJitLoopSafeTypeId(module, ret_type_id)) {
    if (out_error) *out_error = "System.FFI.call result is not JIT loop safe";
    return false;
  }
  for (uint32_t type_id : arg_type_ids) {
    if (!IsDlJitLoopSafeTypeId(module, type_id)) {
      if (out_error) *out_error = "System.FFI.call parameter is not JIT loop safe";
      return false;
    }
  }
  return true;
}

bool ValidateDlNativeAbiSignature(const SbcModule& module,
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
  if (has_ret && !BuildExternalDlAbiTypeInfo(module, ret_type_id, &result_abi, out_error)) return false;
  if (!Simple::VM::Runtime::ValidateExternalCAbiTypeInfos(parameter_abi, result_abi, out_error)) return false;

  DlAbiCache cache;
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
    if (out_error) *out_error = "System.FFI.call ffi_prep_cif failed";
    return false;
  }
  for (uint32_t type_id : arg_type_ids) {
    if (IsStructTypeId(module, type_id) && !PrepareStructOffsets(module, type_id, cache, out_error)) return false;
  }
  if (has_ret && IsStructTypeId(module, ret_type_id) && !PrepareStructOffsets(module, ret_type_id, cache, out_error)) {
    return false;
  }
  return true;
}

DynamicDlAbiValidation AnalyzeDynamicDlCallSignature(const SbcModule& module,
                                                    uint32_t ret_type_id,
                                                    bool has_ret,
                                                    const std::vector<uint32_t>& arg_type_ids) {
  DynamicDlAbiValidation result;
  std::string error;
  result.vm_marshal_supported = ValidateDlVmMarshalSignature(module,
                                                             ret_type_id,
                                                             has_ret,
                                                             arg_type_ids,
                                                             &result.may_allocate,
                                                             &result.needs_roots,
                                                             &error);
  if (!result.vm_marshal_supported) {
    result.reason = error;
    return result;
  }
  error.clear();
  result.abi_valid = ValidateDlNativeAbiSignature(module, ret_type_id, has_ret, arg_type_ids, &error);
  if (!result.abi_valid) {
    result.reason = error;
    return result;
  }
  result.may_block = true;
  result.jit_helper_safe = true;
  error.clear();
  result.jit_loop_safe = ValidateDlJitLoopSignature(module, ret_type_id, has_ret, arg_type_ids, &error);
  if (!result.jit_loop_safe && result.reason.empty()) result.reason = error;
  return result;
}

DynamicDlAbiValidation AnalyzeDynamicDlFunctionSignature(const SbcModule& module,
                                                        uint32_t ret_type_id,
                                                        bool has_ret,
                                                        const std::vector<uint32_t>& param_type_ids) {
  DynamicDlAbiValidation result;
  if (param_type_ids.empty()) {
    result.reason = "System.FFI.call missing function pointer parameter";
    return result;
  }
  const uint32_t ptr_type_id = param_type_ids.front();
  if (ptr_type_id >= module.types.size()) {
    result.reason = "System.FFI.call function pointer type id out of range";
    return result;
  }
  const auto ptr_kind = static_cast<TypeKind>(module.types[ptr_type_id].kind);
  if (ptr_kind != TypeKind::Ptr) {
    result.reason = "System.FFI.call function pointer must use pointer ABI type";
    return result;
  }
  std::vector<uint32_t> arg_type_ids(param_type_ids.begin() + 1, param_type_ids.end());
  return AnalyzeDynamicDlCallSignature(module, ret_type_id, has_ret, arg_type_ids);
}

bool ValidateDynamicDlCallSignature(const SbcModule& module,
                                    uint32_t ret_type_id,
                                    bool has_ret,
                                    const std::vector<uint32_t>& arg_type_ids,
                                    std::string* out_error) {
  DynamicDlAbiValidation result = AnalyzeDynamicDlCallSignature(module, ret_type_id, has_ret, arg_type_ids);
  if (result.abi_valid && result.vm_marshal_supported) return true;
  if (out_error) *out_error = result.reason;
  return false;
}

bool ValidateDynamicDlFunctionSignature(const SbcModule& module,
                                        uint32_t ret_type_id,
                                        bool has_ret,
                                        const std::vector<uint32_t>& param_type_ids,
                                        std::string* out_error) {
  if (param_type_ids.empty()) {
    if (out_error) *out_error = "System.FFI.call missing function pointer parameter";
    return false;
  }
  const uint32_t ptr_type_id = param_type_ids.front();
  if (ptr_type_id >= module.types.size()) {
    if (out_error) *out_error = "System.FFI.call function pointer type id out of range";
    return false;
  }
  const auto ptr_kind = static_cast<TypeKind>(module.types[ptr_type_id].kind);
  if (ptr_kind != TypeKind::Ptr) {
    if (out_error) *out_error = "System.FFI.call function pointer must use pointer ABI type";
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
    if (out_error) *out_error = "System.FFI.call ffi_prep_cif failed";
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
      if (out_error) *out_error = "System.FFI.call arg index out of range";
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
                                  arg_storage[i].data(),
                                  out_error)) {
        return false;
      }
    } else if (!FillScalarArgStorage(module,
                                     type_id,
                                     args[arg_base + i],
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
    if (out_error) *out_error = "System.FFI.call missing return slot";
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
    case TypeKind::I8: return PackDlReturn<int8_t>(*reinterpret_cast<int8_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::I16: return PackDlReturn<int16_t>(*reinterpret_cast<int16_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::I32: return PackDlReturn<int32_t>(*reinterpret_cast<int32_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::I64: return PackDlReturn<int64_t>(*reinterpret_cast<int64_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::U8: return PackDlReturn<uint8_t>(*reinterpret_cast<uint8_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::U16: return PackDlReturn<uint16_t>(*reinterpret_cast<uint16_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::U32: return PackDlReturn<uint32_t>(*reinterpret_cast<uint32_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::U64:
      return PackDlReturn<uint64_t>(*reinterpret_cast<uint64_t*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::Ptr:
      *out_ret = static_cast<Slot>(reinterpret_cast<uintptr_t>(
          *reinterpret_cast<void**>(ret_storage.data())));
      return true;
    case TypeKind::ISize:
      *out_ret = PackI64(static_cast<int64_t>(
          *reinterpret_cast<intptr_t*>(ret_storage.data())));
      return true;
    case TypeKind::USize:
      *out_ret = static_cast<Slot>(
          *reinterpret_cast<uintptr_t*>(ret_storage.data()));
      return true;
    case TypeKind::F32: return PackDlReturn<float>(*reinterpret_cast<float*>(ret_storage.data()), out_ret, out_error);
    case TypeKind::F64: return PackDlReturn<double>(*reinterpret_cast<double*>(ret_storage.data()), out_ret, out_error);
    default:
      if (out_error) *out_error = "System.FFI.call unsupported return type";
      return false;
  }
}



} // namespace Simple::VM::Ffi
