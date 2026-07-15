#include "gc/struct_value.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace Simple::VM::GC {
namespace {

bool GetStructObject(const Simple::Byte::SbcModule& module,
                     const Heap& heap,
                     uint32_t handle,
                     const HeapObject** object,
                     std::string* error) {
  const HeapObject* value = heap.Get(handle);
  if (!value || value->header.kind != ObjectKind::Aggregate ||
      value->header.type_id >= module.types.size() ||
      !Simple::Byte::IsStableStructType(module.types[value->header.type_id])) {
    if (error) *error = "invalid struct value";
    return false;
  }
  const auto& type = module.types[value->header.type_id];
  if (type.field_start + type.field_count > module.fields.size()) {
    if (error) *error = "struct field range is out of bounds";
    return false;
  }
  *object = value;
  return true;
}

bool CloneImpl(const Simple::Byte::SbcModule& module,
               Heap& heap,
               uint32_t source,
               uint32_t* clone,
               std::string* error) {
  const HeapObject* value = nullptr;
  if (!clone || !GetStructObject(module, heap, source, &value, error)) return false;
  const uint32_t type_id = value->header.type_id;
  const uint32_t size = value->header.size;
  const std::vector<uint8_t> payload = value->payload;
  const uint32_t result = heap.Allocate(ObjectKind::Aggregate, type_id, size);
  HeapObject* output = heap.Get(result);
  if (!output) {
    if (error) *error = "struct copy allocation failed";
    return false;
  }
  output->payload = payload;
  const auto& type = module.types[type_id];
  for (uint32_t i = 0; i < type.field_count; ++i) {
    const auto& field = module.fields[type.field_start + i];
    if (field.type_id >= module.types.size() ||
        !Simple::Byte::IsStableStructType(module.types[field.type_id])) {
      continue;
    }
    if (field.offset + 4 > payload.size()) {
      if (error) *error = "nested struct field is out of bounds";
      return false;
    }
    const uint32_t nested = ReadU32Payload(payload, field.offset);
    if (nested == HeapLayout::kNullRef) continue;
    uint32_t nested_copy = HeapLayout::kNullRef;
    if (!CloneImpl(module, heap, nested, &nested_copy, error)) return false;
    output = heap.Get(result);
    if (!output) {
      if (error) *error = "struct copy lost its destination";
      return false;
    }
    WriteU32Payload(output->payload, field.offset, nested_copy);
  }
  *clone = result;
  return true;
}

uint32_t StorageWidth(const Simple::Byte::TypeRow& type) {
  if (static_cast<Simple::Byte::TypeKind>(type.kind) ==
          Simple::Byte::TypeKind::Unspecified &&
      (Simple::Byte::IsManagedClassType(type) ||
       Simple::Byte::IsStableStructType(type))) {
    return 4;
  }
  return type.size;
}

bool EqualImpl(const Simple::Byte::SbcModule& module,
               const Heap& heap,
               uint32_t left,
               uint32_t right,
               std::unordered_set<uint64_t>* active,
               bool* equal,
               std::string* error) {
  const HeapObject* lhs = nullptr;
  const HeapObject* rhs = nullptr;
  if (!equal || !active || !GetStructObject(module, heap, left, &lhs, error) ||
      !GetStructObject(module, heap, right, &rhs, error)) {
    return false;
  }
  if (lhs->header.type_id != rhs->header.type_id) {
    *equal = false;
    return true;
  }
  const uint64_t pair = (static_cast<uint64_t>(left) << 32u) | right;
  if (!active->insert(pair).second) {
    if (error) *error = "recursive struct value detected";
    return false;
  }
  const auto& type = module.types[lhs->header.type_id];
  for (uint32_t i = 0; i < type.field_count; ++i) {
    const auto& field = module.fields[type.field_start + i];
    if (field.type_id >= module.types.size()) {
      if (error) *error = "struct field type is out of bounds";
      return false;
    }
    const auto& field_type = module.types[field.type_id];
    const uint32_t width = StorageWidth(field_type);
    if (width == 0 || field.offset + width > lhs->payload.size() ||
        field.offset + width > rhs->payload.size()) {
      if (error) *error = "struct equality field is out of bounds";
      return false;
    }
    if (Simple::Byte::IsStableStructType(field_type)) {
      const uint32_t lhs_nested = ReadU32Payload(lhs->payload, field.offset);
      const uint32_t rhs_nested = ReadU32Payload(rhs->payload, field.offset);
      if (lhs_nested == HeapLayout::kNullRef || rhs_nested == HeapLayout::kNullRef) {
        if (lhs_nested != rhs_nested) {
          *equal = false;
          return true;
        }
        continue;
      }
      bool nested_equal = false;
      if (!EqualImpl(module, heap, lhs_nested, rhs_nested, active,
                     &nested_equal, error)) {
        return false;
      }
      if (!nested_equal) {
        *equal = false;
        return true;
      }
    } else if (static_cast<Simple::Byte::TypeKind>(field_type.kind) ==
               Simple::Byte::TypeKind::F32) {
      const uint32_t lhs_bits = ReadU32Payload(lhs->payload, field.offset);
      const uint32_t rhs_bits = ReadU32Payload(rhs->payload, field.offset);
      float lhs_value = 0.0f;
      float rhs_value = 0.0f;
      std::memcpy(&lhs_value, &lhs_bits, sizeof(lhs_value));
      std::memcpy(&rhs_value, &rhs_bits, sizeof(rhs_value));
      if (lhs_value != rhs_value) {
        *equal = false;
        return true;
      }
    } else if (static_cast<Simple::Byte::TypeKind>(field_type.kind) ==
               Simple::Byte::TypeKind::F64) {
      const uint64_t lhs_bits = ReadU64Payload(lhs->payload, field.offset);
      const uint64_t rhs_bits = ReadU64Payload(rhs->payload, field.offset);
      double lhs_value = 0.0;
      double rhs_value = 0.0;
      std::memcpy(&lhs_value, &lhs_bits, sizeof(lhs_value));
      std::memcpy(&rhs_value, &rhs_bits, sizeof(rhs_value));
      if (lhs_value != rhs_value) {
        *equal = false;
        return true;
      }
    } else if (!std::equal(lhs->payload.begin() + field.offset,
                           lhs->payload.begin() + field.offset + width,
                           rhs->payload.begin() + field.offset)) {
      *equal = false;
      return true;
    }
  }
  active->erase(pair);
  *equal = true;
  return true;
}

} // namespace

bool CloneStructValue(const Simple::Byte::SbcModule& module,
                      Heap& heap,
                      uint32_t source,
                      uint32_t* clone,
                      std::string* error) {
  return CloneImpl(module, heap, source, clone, error);
}

bool StructValuesEqual(const Simple::Byte::SbcModule& module,
                       const Heap& heap,
                       uint32_t left,
                       uint32_t right,
                       bool* equal,
                       std::string* error) {
  std::unordered_set<uint64_t> active;
  return EqualImpl(module, heap, left, right, &active, equal, error);
}

} // namespace Simple::VM::GC
