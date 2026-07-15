#include "heap.h"

#include <cstddef>
#include <utility>

namespace Simple::VM {

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, std::size_t offset) {
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

uint64_t ReadU64Payload(const std::vector<uint8_t>& payload, std::size_t offset) {
  return static_cast<uint64_t>(payload[offset]) |
         (static_cast<uint64_t>(payload[offset + 1]) << 8) |
         (static_cast<uint64_t>(payload[offset + 2]) << 16) |
         (static_cast<uint64_t>(payload[offset + 3]) << 24) |
         (static_cast<uint64_t>(payload[offset + 4]) << 32) |
         (static_cast<uint64_t>(payload[offset + 5]) << 40) |
         (static_cast<uint64_t>(payload[offset + 6]) << 48) |
         (static_cast<uint64_t>(payload[offset + 7]) << 56);
}

uint16_t ReadU16Payload(const std::vector<uint8_t>& payload, std::size_t offset) {
  return static_cast<uint16_t>(payload[offset]) |
         (static_cast<uint16_t>(payload[offset + 1]) << 8);
}

void WriteU32Payload(std::vector<uint8_t>& payload, std::size_t offset, uint32_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void WriteU64Payload(std::vector<uint8_t>& payload, std::size_t offset, uint64_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
  payload[offset + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
  payload[offset + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
  payload[offset + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
  payload[offset + 7] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

void WriteU16Payload(std::vector<uint8_t>& payload, std::size_t offset, uint16_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

bool EnsureListCapacity(HeapObject* obj, uint32_t min_capacity, std::size_t elem_size) {
  if (!obj) return false;
  uint32_t capacity = ReadU32Payload(obj->payload, HeapLayout::kListCapacityOffset);
  if (capacity >= min_capacity) return true;
  uint32_t new_capacity = capacity ? capacity : 1u;
  while (new_capacity < min_capacity) {
    new_capacity = (new_capacity < (1u << 31)) ? (new_capacity * 2u) : min_capacity;
  }
  const std::size_t new_size = HeapLayout::ListPayloadSize(new_capacity, static_cast<uint32_t>(elem_size));
  obj->payload.resize(new_size);
  WriteU32Payload(obj->payload, HeapLayout::kListCapacityOffset, new_capacity);
  return true;
}

std::u16string AsciiToU16(const std::string& text) {
  std::u16string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    out.push_back(static_cast<char16_t>(c));
  }
  return out;
}

std::string U16ToAscii(const std::u16string& text) {
  std::string out;
  out.reserve(text.size());
  for (char16_t c : text) {
    if (c <= 0x7Fu) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('?');
    }
  }
  return out;
}

uint32_t CreateString(Heap& heap, const std::u16string& text) {
  uint32_t length = static_cast<uint32_t>(text.size());
  uint32_t size = static_cast<uint32_t>(HeapLayout::StringPayloadSize(length));
  uint32_t handle = heap.Allocate(ObjectKind::String, 0, size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return HeapLayout::kNullRef;
  WriteU32Payload(obj->payload, HeapLayout::kStringLengthOffset, length);
  for (uint32_t i = 0; i < length; ++i) {
    WriteU16Payload(obj->payload, HeapLayout::StringCodeUnitOffset(i), text[i]);
  }
  return handle;
}

std::u16string ReadString(const HeapObject* obj) {
  if (!obj || obj->header.kind != ObjectKind::String) return {};
  uint32_t length = ReadU32Payload(obj->payload, HeapLayout::kStringLengthOffset);
  std::u16string out;
  out.resize(length);
  for (uint32_t i = 0; i < length; ++i) {
    out[i] = static_cast<char16_t>(ReadU16Payload(obj->payload, HeapLayout::StringCodeUnitOffset(i)));
  }
  return out;
}

uint32_t CreateBytes(Heap& heap, const std::vector<uint8_t>& bytes) {
  const uint32_t length = static_cast<uint32_t>(bytes.size());
  const uint32_t size = static_cast<uint32_t>(HeapLayout::BytesPayloadSize(length));
  const uint32_t handle = heap.Allocate(ObjectKind::Bytes, 0, size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return HeapLayout::kNullRef;
  WriteU32Payload(obj->payload, HeapLayout::kBytesLengthOffset, length);
  for (uint32_t i = 0; i < length; ++i) {
    obj->payload[HeapLayout::BytesElementOffset(i)] = bytes[i];
  }
  return handle;
}

std::vector<uint8_t> ReadBytes(const HeapObject* obj) {
  if (!obj || obj->header.kind != ObjectKind::Bytes) return {};
  const uint32_t length = ReadU32Payload(obj->payload, HeapLayout::kBytesLengthOffset);
  std::vector<uint8_t> out;
  out.reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    const std::size_t offset = HeapLayout::BytesElementOffset(i);
    if (offset >= obj->payload.size()) return {};
    out.push_back(obj->payload[offset]);
  }
  return out;
}

void Heap::SetLimits(uint32_t max_objects, uint64_t max_bytes) {
  max_objects_ = max_objects;
  max_bytes_ = max_bytes;
}

void Heap::SetAggregateTraceDescriptors(std::vector<AggregateTraceDescriptor> descriptors) {
  aggregate_trace_descriptors_ = std::move(descriptors);
}

uint32_t Heap::Allocate(ObjectKind kind, uint32_t type_id, uint32_t size) {
  if (max_objects_ != 0 && live_objects_ >= max_objects_) return HeapLayout::kNullRef;
  if (max_bytes_ != 0 && (live_bytes_ > max_bytes_ || static_cast<uint64_t>(size) > max_bytes_ - live_bytes_)) {
    return HeapLayout::kNullRef;
  }
  if (objects_.empty()) {
    HeapObject null_object;
    null_object.header.kind = ObjectKind::String;
    null_object.header.size = 0;
    null_object.header.type_id = 0;
    null_object.header.marked = 0;
    null_object.header.alive = 0;
    objects_.push_back(std::move(null_object));
  }
  if (!free_list_.empty()) {
    uint32_t handle = free_list_.back();
    free_list_.pop_back();
    HeapObject& obj = objects_[handle];
    obj.header.kind = kind;
    obj.header.size = size;
    obj.header.type_id = type_id;
    obj.header.marked = 0;
    obj.header.alive = 1;
    obj.payload.assign(size, 0);
    ++live_objects_;
    live_bytes_ += size;
    return handle;
  }

  HeapObject obj;
  obj.header.kind = kind;
  obj.header.size = size;
  obj.header.type_id = type_id;
  obj.header.marked = 0;
  obj.header.alive = 1;
  obj.payload.resize(size);
  objects_.push_back(std::move(obj));
  ++live_objects_;
  live_bytes_ += size;
  return static_cast<uint32_t>(objects_.size() - 1);
}

HeapObject* Heap::Get(uint32_t handle) {
  if (handle >= objects_.size()) return nullptr;
  if (!objects_[handle].header.alive) return nullptr;
  return &objects_[handle];
}

const HeapObject* Heap::Get(uint32_t handle) const {
  if (handle >= objects_.size()) return nullptr;
  if (!objects_[handle].header.alive) return nullptr;
  return &objects_[handle];
}

void Heap::Mark(uint32_t handle) {
  HeapObject* obj = Get(handle);
  if (!obj) return;
  if (obj->header.marked) return;
  obj->header.marked = 1;

  auto mark_payload_refs = [&](std::size_t begin, std::size_t end) {
    if (end > obj->payload.size()) end = obj->payload.size();
    for (std::size_t offset = begin; offset + 4 <= end; offset += 4) {
      uint32_t ref = ReadU32Payload(obj->payload, offset);
      if (ref != HeapLayout::kNullRef) Mark(ref);
    }
  };

  switch (obj->header.kind) {
    case ObjectKind::String:
    case ObjectKind::Bytes:
      return;
    case ObjectKind::Array:
      if (obj->header.type_id != 0) mark_payload_refs(HeapLayout::kArrayDataOffset, obj->payload.size());
      return;
    case ObjectKind::List:
      if (obj->header.type_id != 0) mark_payload_refs(HeapLayout::kListDataOffset, obj->payload.size());
      return;
    case ObjectKind::Aggregate: {
      if (obj->header.type_id >= aggregate_trace_descriptors_.size() ||
          !aggregate_trace_descriptors_[obj->header.type_id].configured) {
        mark_payload_refs(0, obj->payload.size());
        return;
      }
      const AggregateTraceDescriptor& descriptor =
          aggregate_trace_descriptors_[obj->header.type_id];
      auto mark_offsets = [&](const std::vector<uint32_t>& offsets) {
        for (uint32_t offset : offsets) {
          if (static_cast<std::size_t>(offset) + 4 > obj->payload.size()) continue;
          const uint32_t ref = ReadU32Payload(obj->payload, offset);
          if (ref != HeapLayout::kNullRef) Mark(ref);
        }
      };
      mark_offsets(descriptor.refs);
      if (descriptor.branch_on_tag &&
          static_cast<std::size_t>(descriptor.tag_offset) + 4 <= obj->payload.size()) {
        const uint32_t tag = ReadU32Payload(obj->payload, descriptor.tag_offset);
        mark_offsets(tag == 0 ? descriptor.zero_tag_refs : descriptor.nonzero_tag_refs);
      }
      return;
    }
    case ObjectKind::Closure: {
      if (obj->payload.size() < HeapLayout::kClosureUpvalueDataOffset) return;
      uint32_t upvalue_count = ReadU32Payload(obj->payload, HeapLayout::kClosureUpvalueCountOffset);
      for (uint32_t i = 0; i < upvalue_count; ++i) {
        std::size_t offset = HeapLayout::ClosureUpvalueOffset(i);
        if (offset + 4 > obj->payload.size()) break;
        uint32_t ref = ReadU32Payload(obj->payload, offset);
        if (ref != HeapLayout::kNullRef) Mark(ref);
      }
      return;
    }
    case ObjectKind::Promise: {
      if (obj->payload.size() < HeapLayout::kPromiseArgumentDataOffset) return;
      const uint32_t state = ReadU32Payload(obj->payload, HeapLayout::kPromiseStateOffset);
      if (state == HeapLayout::kPromiseStateCompleted &&
          ReadU32Payload(obj->payload, HeapLayout::kPromiseResultIsRefOffset) != 0) {
        const uint32_t ref = static_cast<uint32_t>(
            ReadU64Payload(obj->payload, HeapLayout::kPromiseResultOffset));
        if (ref != HeapLayout::kNullRef) Mark(ref);
      }
      if (state != HeapLayout::kPromiseStatePending &&
          state != HeapLayout::kPromiseStateRunning) return;
      const uint32_t count =
          ReadU32Payload(obj->payload, HeapLayout::kPromiseArgumentCountOffset);
      for (uint32_t i = 0; i < count; ++i) {
        const std::size_t value_offset = HeapLayout::PromiseArgumentValueOffset(i);
        const std::size_t flag_offset = HeapLayout::PromiseArgumentIsRefOffset(i);
        if (flag_offset + 4 > obj->payload.size()) break;
        if (ReadU32Payload(obj->payload, flag_offset) == 0) continue;
        const uint32_t ref = static_cast<uint32_t>(ReadU64Payload(obj->payload, value_offset));
        if (ref != HeapLayout::kNullRef) Mark(ref);
      }
      return;
    }
  }
}

void Heap::ResetMarks() {
  for (auto& obj : objects_) {
    if (!obj.header.alive) continue;
    obj.header.marked = 0;
  }
}

void Heap::Sweep() {
  for (uint32_t i = 0; i < objects_.size(); ++i) {
    HeapObject& obj = objects_[i];
    if (!obj.header.alive) continue;
    if (obj.header.marked) {
      obj.header.marked = 0;
      continue;
    }
    if (live_objects_ > 0) --live_objects_;
    if (live_bytes_ >= obj.header.size) live_bytes_ -= obj.header.size;
    else live_bytes_ = 0;
    obj.header.alive = 0;
    obj.header.marked = 0;
    obj.header.size = 0;
    obj.header.type_id = 0;
    obj.payload.clear();
    obj.payload.shrink_to_fit();
    free_list_.push_back(i);
  }
}

} // namespace Simple::VM
