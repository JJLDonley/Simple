#ifndef SIMPLE_VM_RUNTIME_VALUES_H
#define SIMPLE_VM_RUNTIME_VALUES_H

#include <cstdint>
#include <cstring>

#include "heap.h"
#include "interpreter/stack.h"

namespace Simple::VM::Runtime {

using Slot = uint64_t;

inline float BitsToF32(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

inline double BitsToF64(uint64_t bits) {
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

inline uint32_t F32ToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline uint64_t F64ToBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline Slot PackI32(int32_t value) {
  return static_cast<uint64_t>(static_cast<uint32_t>(value));
}

inline int32_t UnpackI32(Slot value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

inline Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

inline int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
}

inline uint32_t UnpackU32Bits(Slot value) {
  return static_cast<uint32_t>(value);
}

inline uint64_t UnpackU64Bits(Slot value) {
  return value;
}

inline Slot PackF32Bits(uint32_t bits) {
  return static_cast<uint64_t>(bits);
}

inline Slot PackF64Bits(uint64_t bits) {
  return bits;
}

inline Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

inline uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value);
}

inline bool IsNullRef(Slot value) {
  return UnpackRef(value) == HeapLayout::kNullRef;
}

} // namespace Simple::VM::Runtime

#endif // SIMPLE_VM_RUNTIME_VALUES_H
