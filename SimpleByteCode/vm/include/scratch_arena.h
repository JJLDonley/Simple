#ifndef SIMPLE_VM_SCRATCH_ARENA_H
#define SIMPLE_VM_SCRATCH_ARENA_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace simplevm {

class ScratchArena {
 public:
  explicit ScratchArena(std::size_t initial_capacity = 0) : buffer_(initial_capacity), offset_(0) {}

  std::size_t Mark() const { return offset_; }
  void Reset(std::size_t mark = 0) { offset_ = mark; }
  std::size_t Used() const { return offset_; }
  std::size_t Capacity() const { return buffer_.size(); }

  uint8_t* Allocate(std::size_t size, std::size_t align = 8) {
    if (size == 0) return nullptr;
    if (align == 0 || (align & (align - 1)) != 0) align = 1;
    std::size_t aligned = (offset_ + (align - 1)) & ~(align - 1);
    std::size_t required = aligned + size;
    if (required > buffer_.size()) buffer_.resize(required);
    uint8_t* out = buffer_.data() + aligned;
    offset_ = required;
    return out;
  }

 private:
  std::vector<uint8_t> buffer_;
  std::size_t offset_;
};

class ScratchScope {
 public:
  explicit ScratchScope(ScratchArena& arena) : arena_(arena), mark_(arena.Mark()) {}
  ~ScratchScope() { arena_.Reset(mark_); }

  ScratchScope(const ScratchScope&) = delete;
  ScratchScope& operator=(const ScratchScope&) = delete;

 private:
  ScratchArena& arena_;
  std::size_t mark_;
};

} // namespace simplevm

#endif // SIMPLE_VM_SCRATCH_ARENA_H
