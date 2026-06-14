#include "interpreter/globals.h"

#include <string>
#include <vector>

namespace Simple::VM::Interpreter {
namespace {

constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  if (offset + 4 > payload.size()) return;
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint32_t CreateString(Heap& heap, const std::u16string& text) {
  const uint32_t len = static_cast<uint32_t>(text.size());
  const uint32_t handle = heap.Allocate(ObjectKind::String, 0,
                                        static_cast<uint32_t>(HeapLayout::StringPayloadSize(len)));
  HeapObject* obj = heap.Get(handle);
  if (!obj) return kNullRef;
  WriteU32Payload(obj->payload, HeapLayout::kStringLengthOffset, len);
  for (uint32_t i = 0; i < len; ++i) {
    const uint16_t cu = static_cast<uint16_t>(text[i]);
    const size_t off = HeapLayout::StringCodeUnitOffset(i);
    if (off + 1 >= obj->payload.size()) break;
    obj->payload[off] = static_cast<uint8_t>(cu & 0xFFu);
    obj->payload[off + 1] = static_cast<uint8_t>((cu >> 8) & 0xFFu);
  }
  return handle;
}

Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

} // namespace

bool LoadConstStringSlot(const Simple::Byte::SbcModule& module, Heap& heap, uint32_t const_id, Slot& out_value) {
  if (const_id + 8 > module.const_pool.size()) return false;
  const uint32_t kind = ReadU32Payload(module.const_pool, const_id);
  if (kind != 0) return false;
  const uint32_t str_offset = ReadU32Payload(module.const_pool, const_id + 4);
  if (str_offset >= module.const_pool.size()) return false;
  const char* base = reinterpret_cast<const char*>(module.const_pool.data() + str_offset);
  std::u16string text;
  for (size_t i = 0; str_offset + i < module.const_pool.size(); ++i) {
    const char c = base[i];
    if (c == '\0') break;
    text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  }
  const uint32_t handle = CreateString(heap, text);
  if (handle == kNullRef) return false;
  out_value = PackRef(handle);
  return true;
}

bool IsRefLikeGlobal(const Simple::Byte::SbcModule& module, size_t global_index) {
  if (global_index >= module.globals.size()) return false;
  const uint32_t type_id = module.globals[global_index].type_id;
  if (type_id >= module.types.size()) return false;
  const auto& row = module.types[type_id];
  const auto kind = static_cast<Simple::Byte::TypeKind>(row.kind);
  if (kind == Simple::Byte::TypeKind::Ref || kind == Simple::Byte::TypeKind::String) return true;
  return kind == Simple::Byte::TypeKind::Unspecified && (row.flags & 0x1u) != 0u;
}

} // namespace Simple::VM::Interpreter
