#include "heap.h"

#include <cstddef>

namespace Simple::VM {

namespace {

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, std::size_t offset) {
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

} // namespace

void Heap::SetLimits(uint32_t max_objects, uint64_t max_bytes) {
  max_objects_ = max_objects;
  max_bytes_ = max_bytes;
}

uint32_t Heap::Allocate(ObjectKind kind, uint32_t type_id, uint32_t size) {
  if (max_objects_ != 0 && live_objects_ >= max_objects_) return HeapLayout::kNullRef;
  if (max_bytes_ != 0 && (live_bytes_ > max_bytes_ || static_cast<uint64_t>(size) > max_bytes_ - live_bytes_)) {
    return HeapLayout::kNullRef;
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
      return;
    case ObjectKind::Array:
      if (obj->header.type_id != 0) mark_payload_refs(HeapLayout::kArrayDataOffset, obj->payload.size());
      return;
    case ObjectKind::List:
      if (obj->header.type_id != 0) mark_payload_refs(HeapLayout::kListDataOffset, obj->payload.size());
      return;
    case ObjectKind::Artifact:
      mark_payload_refs(0, obj->payload.size());
      return;
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
