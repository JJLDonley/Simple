#include "native/arg_utils.h"

namespace Simple::VM::Native {
namespace {

Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

std::string ReadStringAscii(const HeapObject* obj) {
  if (!obj || obj->header.kind != ObjectKind::String || obj->payload.size() < 4) return {};
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  if (4u + length * 2u > obj->payload.size()) return {};
  std::string out;
  out.reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    const size_t offset = 4u + i * 2u;
    const uint16_t ch = obj->payload[offset] | (static_cast<uint16_t>(obj->payload[offset + 1]) << 8u);
    out.push_back(ch <= 0x7fu ? static_cast<char>(ch) : '?');
  }
  return out;
}

} // namespace

HeapObject* NativeArgHeapObject(NativeCallContext& context, size_t index) {
  if (!context.heap || index >= context.args.size()) return nullptr;
  return context.heap->Get(static_cast<uint32_t>(context.args[index] & 0xffffffffu));
}

bool ReadStringArg(NativeCallContext& context, size_t index, std::string* out_value) {
  if (!out_value) return false;
  HeapObject* obj = NativeArgHeapObject(context, index);
  if (!obj || obj->header.kind != ObjectKind::String) return false;
  *out_value = ReadStringAscii(obj);
  return true;
}

bool ReadByteSequence(NativeCallContext& context, size_t index, std::vector<int32_t>* out) {
  if (!out) return false;
  HeapObject* obj = NativeArgHeapObject(context, index);
  if (!obj || obj->payload.size() < 4) return false;
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  out->clear();
  out->reserve(length);
  if (obj->header.kind == ObjectKind::Bytes) {
    if (HeapLayout::kBytesDataOffset + static_cast<size_t>(length) > obj->payload.size()) return false;
    for (uint32_t i = 0; i < length; ++i) {
      out->push_back(static_cast<int32_t>(obj->payload[HeapLayout::BytesElementOffset(i)]));
    }
    return true;
  }
  if (obj->header.kind != ObjectKind::List && obj->header.kind != ObjectKind::Array) return false;
  const size_t elem_base = obj->header.kind == ObjectKind::List ? 8u : 4u;
  if (elem_base + static_cast<size_t>(length) * 4u > obj->payload.size()) return false;
  for (uint32_t i = 0; i < length; ++i) {
    const size_t offset = elem_base + i * 4u;
    const uint32_t value = obj->payload[offset] |
                           (static_cast<uint32_t>(obj->payload[offset + 1]) << 8u) |
                           (static_cast<uint32_t>(obj->payload[offset + 2]) << 16u) |
                           (static_cast<uint32_t>(obj->payload[offset + 3]) << 24u);
    out->push_back(static_cast<int32_t>(value));
  }
  return true;
}

Slot CreateByteList(Heap& heap, const std::vector<uint32_t>& values) {
  const uint32_t length = static_cast<uint32_t>(values.size());
  const uint32_t size = 8u + length * 4u;
  const uint32_t handle = heap.Allocate(ObjectKind::List, 0, size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return PackRef(HeapLayout::kNullRef);
  WriteU32(obj->payload, 0, length);
  WriteU32(obj->payload, 4, length);
  for (uint32_t i = 0; i < length; ++i) {
    WriteU32(obj->payload, 8u + i * 4u, values[i] & 0xffu);
  }
  return PackRef(handle);
}

void WriteU32(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  if (offset + 4 > payload.size()) return;
  payload[offset] = static_cast<uint8_t>(value & 0xffu);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

uint32_t ReadU32(const std::vector<uint8_t>& payload, size_t offset) {
  if (offset + 4 > payload.size()) return 0;
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8u) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16u) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24u);
}

} // namespace Simple::VM::Native
