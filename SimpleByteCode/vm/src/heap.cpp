#include "heap.h"

namespace simplevm {

uint32_t Heap::Allocate(ObjectKind kind, uint32_t type_id, uint32_t size) {
  HeapObject obj;
  obj.header.kind = kind;
  obj.header.size = size;
  obj.header.type_id = type_id;
  obj.header.marked = 0;
  obj.payload.resize(size);
  objects_.push_back(std::move(obj));
  return static_cast<uint32_t>(objects_.size() - 1);
}

HeapObject* Heap::Get(uint32_t handle) {
  if (handle >= objects_.size()) return nullptr;
  return &objects_[handle];
}

const HeapObject* Heap::Get(uint32_t handle) const {
  if (handle >= objects_.size()) return nullptr;
  return &objects_[handle];
}

void Heap::Mark(uint32_t handle) {
  HeapObject* obj = Get(handle);
  if (!obj) return;
  obj->header.marked = 1;
}

void Heap::ResetMarks() {
  for (auto& obj : objects_) {
    obj.header.marked = 0;
  }
}

void Heap::Sweep() {
  for (auto& obj : objects_) {
    obj.header.marked = 0;
  }
}

} // namespace simplevm
