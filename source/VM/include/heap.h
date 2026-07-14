#ifndef SIMPLE_VM_HEAP_H
#define SIMPLE_VM_HEAP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Simple::VM {

enum class ObjectKind : uint8_t {
  String,
  Bytes,
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

struct ArtifactTraceDescriptor {
  bool configured = false;
  uint32_t tag_offset = 0;
  bool branch_on_tag = false;
  std::vector<uint32_t> refs;
  std::vector<uint32_t> zero_tag_refs;
  std::vector<uint32_t> nonzero_tag_refs;
};

namespace HeapLayout {

constexpr uint32_t kNullRef = 0u;

constexpr std::size_t kStringLengthOffset = 0;
constexpr std::size_t kStringDataOffset = 4;
constexpr std::size_t StringPayloadSize(uint32_t code_units) {
  return kStringDataOffset + static_cast<std::size_t>(code_units) * 2u;
}
constexpr std::size_t StringCodeUnitOffset(uint32_t index) {
  return kStringDataOffset + static_cast<std::size_t>(index) * 2u;
}

constexpr std::size_t kBytesLengthOffset = 0;
constexpr std::size_t kBytesDataOffset = 4;
constexpr std::size_t BytesPayloadSize(uint32_t length) {
  return kBytesDataOffset + static_cast<std::size_t>(length);
}
constexpr std::size_t BytesElementOffset(uint32_t index) {
  return kBytesDataOffset + static_cast<std::size_t>(index);
}

constexpr std::size_t kArrayLengthOffset = 0;
constexpr std::size_t kArrayDataOffset = 4;
constexpr std::size_t ArrayPayloadSize(uint32_t length, uint32_t elem_size) {
  return kArrayDataOffset + static_cast<std::size_t>(length) * elem_size;
}
constexpr std::size_t ArrayElementOffset(uint32_t index, uint32_t elem_size) {
  return kArrayDataOffset + static_cast<std::size_t>(index) * elem_size;
}

constexpr std::size_t kListLengthOffset = 0;
constexpr std::size_t kListCapacityOffset = 4;
constexpr std::size_t kListDataOffset = 8;
constexpr std::size_t ListPayloadSize(uint32_t capacity, uint32_t elem_size) {
  return kListDataOffset + static_cast<std::size_t>(capacity) * elem_size;
}
constexpr std::size_t ListElementOffset(uint32_t index, uint32_t elem_size) {
  return kListDataOffset + static_cast<std::size_t>(index) * elem_size;
}

constexpr std::size_t ArtifactPayloadSize(uint32_t byte_size) {
  return byte_size;
}
constexpr std::size_t ArtifactFieldOffset(uint32_t byte_offset) {
  return byte_offset;
}

constexpr std::size_t kClosureMethodIdOffset = 0;
constexpr std::size_t kClosureUpvalueCountOffset = 4;
constexpr std::size_t kClosureUpvalueDataOffset = 8;
constexpr std::size_t ClosurePayloadSize(uint32_t upvalue_count) {
  return kClosureUpvalueDataOffset + static_cast<std::size_t>(upvalue_count) * 4u;
}
constexpr std::size_t ClosureUpvalueOffset(uint32_t index) {
  return kClosureUpvalueDataOffset + static_cast<std::size_t>(index) * 4u;
}

constexpr bool IsRefKind(ObjectKind kind) {
  return kind == ObjectKind::String || kind == ObjectKind::Bytes || kind == ObjectKind::Array ||
         kind == ObjectKind::List || kind == ObjectKind::Artifact || kind == ObjectKind::Closure;
}

} // namespace HeapLayout

class Heap;

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, size_t offset);
uint64_t ReadU64Payload(const std::vector<uint8_t>& payload, size_t offset);
uint16_t ReadU16Payload(const std::vector<uint8_t>& payload, size_t offset);
void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value);
void WriteU64Payload(std::vector<uint8_t>& payload, size_t offset, uint64_t value);
void WriteU16Payload(std::vector<uint8_t>& payload, size_t offset, uint16_t value);
bool EnsureListCapacity(HeapObject* obj, uint32_t min_capacity, size_t elem_size);
std::u16string AsciiToU16(const std::string& text);
std::string U16ToAscii(const std::u16string& text);
uint32_t CreateString(Heap& heap, const std::u16string& text);
std::u16string ReadString(const HeapObject* obj);
uint32_t CreateBytes(Heap& heap, const std::vector<uint8_t>& bytes);
std::vector<uint8_t> ReadBytes(const HeapObject* obj);

class Heap {
 public:
  void SetLimits(uint32_t max_objects, uint64_t max_bytes);
  void SetArtifactTraceDescriptors(std::vector<ArtifactTraceDescriptor> descriptors);
  uint32_t Allocate(ObjectKind kind, uint32_t type_id, uint32_t size);
  HeapObject* Get(uint32_t handle);
  const HeapObject* Get(uint32_t handle) const;
  void Mark(uint32_t handle);
  void Sweep();
  void ResetMarks();

 private:
  std::vector<HeapObject> objects_;
  std::vector<uint32_t> free_list_;
  uint32_t max_objects_ = 0;
  uint64_t max_bytes_ = 0;
  uint32_t live_objects_ = 0;
  uint64_t live_bytes_ = 0;
  std::vector<ArtifactTraceDescriptor> artifact_trace_descriptors_;
};

} // namespace Simple::VM

#endif // SIMPLE_VM_HEAP_H
