#include "heap.h"

namespace simplevm {

uint32_t Heap::Allocate(ObjectKind kind, uint32_t type_id, uint32_t size) {
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
  obj->header.marked = 1;
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
    obj.header.alive = 0;
    obj.header.marked = 0;
    obj.header.size = 0;
    obj.header.type_id = 0;
    obj.payload.clear();
    obj.payload.shrink_to_fit();
    free_list_.push_back(i);
  }
}

} // namespace simplevm
