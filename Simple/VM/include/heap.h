#ifndef SIMPLE_VM_HEAP_H
#define SIMPLE_VM_HEAP_H

#include <cstdint>
#include <vector>

namespace Simple::VM {

enum class ObjectKind : uint8_t {
  String,
  Array,
  List,
  Artifact,
  Closure,
};

struct ObjHeader {
  ObjectKind kind;
  uint32_t size;
  uint32_t type_id;
  uint8_t marked;
  uint8_t alive;
};

struct HeapObject {
  ObjHeader header;
  std::vector<uint8_t> payload;
};

class Heap {
 public:
  uint32_t Allocate(ObjectKind kind, uint32_t type_id, uint32_t size);
  HeapObject* Get(uint32_t handle);
  const HeapObject* Get(uint32_t handle) const;
  void Mark(uint32_t handle);
  void Sweep();
  void ResetMarks();

 private:
  std::vector<HeapObject> objects_;
  std::vector<uint32_t> free_list_;
};

} // namespace Simple::VM

#endif // SIMPLE_VM_HEAP_H
