#include "native/buffer.h"

#include <algorithm>

namespace Simple::VM::Native::Buffer {
namespace {

uint32_t ReadU32(const std::vector<uint8_t>& payload, std::size_t offset) {
  if (offset + 4 > payload.size()) return 0;
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8u) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16u) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24u);
}

void WriteU32(std::vector<uint8_t>& payload, std::size_t offset, uint32_t value) {
  if (offset + 4 > payload.size()) return;
  payload[offset] = static_cast<uint8_t>(value & 0xffu);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

uint32_t Base(const HeapObject* obj) {
  return obj && obj->header.kind == ObjectKind::List ? 8u : 4u;
}

uint32_t ByteAt(const HeapObject* obj, uint32_t index) {
  return ReadU32(obj->payload, Base(obj) + index * 4u) & 0xffu;
}

void SetByte(HeapObject* obj, uint32_t index, uint32_t value) {
  WriteU32(obj->payload, Base(obj) + index * 4u, value & 0xffu);
}

bool HasRange(const HeapObject* obj, uint32_t offset, uint32_t width) {
  return IsBuffer(obj) && offset <= Len(obj) && width <= Len(obj) - offset;
}

} // namespace

bool IsBuffer(const HeapObject* obj) {
  return obj && (obj->header.kind == ObjectKind::List || obj->header.kind == ObjectKind::Array);
}

uint32_t Len(const HeapObject* obj) {
  return IsBuffer(obj) ? ReadU32(obj->payload, 0) : 0u;
}

uint32_t ReadLE(const HeapObject* obj, uint32_t offset, uint32_t width) {
  if (!HasRange(obj, offset, width)) return 0;
  uint32_t value = 0;
  for (uint32_t i = 0; i < width; ++i) value |= ByteAt(obj, offset + i) << (8u * i);
  return value;
}

bool WriteLE(HeapObject* obj, uint32_t offset, uint32_t width, uint32_t value) {
  if (!HasRange(obj, offset, width)) return false;
  for (uint32_t i = 0; i < width; ++i) SetByte(obj, offset + i, value >> (8u * i));
  return true;
}

std::vector<uint32_t> Slice(const HeapObject* obj, uint32_t offset, uint32_t count) {
  if (!IsBuffer(obj) || offset > Len(obj)) return {};
  const uint32_t length = std::min(count, Len(obj) - offset);
  std::vector<uint32_t> values;
  values.reserve(length);
  for (uint32_t i = 0; i < length; ++i) values.push_back(ByteAt(obj, offset + i));
  return values;
}

uint32_t Copy(HeapObject* dst, uint32_t dst_offset, const HeapObject* src, uint32_t src_offset,
              uint32_t count) {
  if (!IsBuffer(dst) || !IsBuffer(src) || dst_offset > Len(dst) || src_offset > Len(src)) return 0;
  uint32_t length = std::min(count, Len(dst) - dst_offset);
  length = std::min(length, Len(src) - src_offset);
  std::vector<uint32_t> tmp;
  tmp.reserve(length);
  for (uint32_t i = 0; i < length; ++i) tmp.push_back(ByteAt(src, src_offset + i));
  for (uint32_t i = 0; i < length; ++i) SetByte(dst, dst_offset + i, tmp[i]);
  return length;
}

} // namespace Simple::VM::Native::Buffer
