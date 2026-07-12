#pragma once

#include "native/registry.h"

#include <cstdint>
#include <cstring>

namespace Simple::VM::Native {

inline Slot PackI32(int32_t value) {
  return static_cast<uint32_t>(value);
}

inline Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

inline Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

inline int32_t UnpackI32(Slot value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

inline int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
}

inline uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value & 0xffffffffu);
}

inline uint32_t UnpackU32Bits(Slot value) {
  return static_cast<uint32_t>(value);
}

inline uint64_t UnpackU64Bits(Slot value) {
  return static_cast<uint64_t>(value);
}

inline Slot PackF32(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline Slot PackF64(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline float UnpackF32(Slot value) {
  const uint32_t bits = UnpackU32Bits(value);
  float result = 0.0f;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

inline double UnpackF64(Slot value) {
  const uint64_t bits = UnpackU64Bits(value);
  double result = 0.0;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

} // namespace Simple::VM::Native
