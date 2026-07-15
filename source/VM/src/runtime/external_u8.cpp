#include "runtime/external_u8.h"

#include <cstdint>

#include "heap.h"

namespace Simple::VM::Runtime {
namespace {

bool IsContinuation(uint8_t value) {
  return (value & 0xC0u) == 0x80u;
}

bool IsValidUtf8(const std::string& value) {
  size_t index = 0;
  while (index < value.size()) {
    const uint8_t first = static_cast<uint8_t>(value[index]);
    if (first <= 0x7Fu) {
      ++index;
      continue;
    }
    if (first >= 0xC2u && first <= 0xDFu) {
      if (index + 1 >= value.size() ||
          !IsContinuation(static_cast<uint8_t>(value[index + 1]))) {
        return false;
      }
      index += 2;
      continue;
    }
    if (first >= 0xE0u && first <= 0xEFu) {
      if (index + 2 >= value.size()) return false;
      const uint8_t second = static_cast<uint8_t>(value[index + 1]);
      const uint8_t third = static_cast<uint8_t>(value[index + 2]);
      if (!IsContinuation(third) ||
          (first == 0xE0u ? second < 0xA0u || second > 0xBFu
                          : (first == 0xEDu ? second < 0x80u || second > 0x9Fu
                                            : !IsContinuation(second)))) {
        return false;
      }
      index += 3;
      continue;
    }
    if (first >= 0xF0u && first <= 0xF4u) {
      if (index + 3 >= value.size()) return false;
      const uint8_t second = static_cast<uint8_t>(value[index + 1]);
      if ((first == 0xF0u && (second < 0x90u || second > 0xBFu)) ||
          (first == 0xF4u && (second < 0x80u || second > 0x8Fu)) ||
          (first != 0xF0u && first != 0xF4u && !IsContinuation(second)) ||
          !IsContinuation(static_cast<uint8_t>(value[index + 2])) ||
          !IsContinuation(static_cast<uint8_t>(value[index + 3]))) {
        return false;
      }
      index += 4;
      continue;
    }
    return false;
  }
  return true;
}

} // namespace

bool BuildExternalU8String(const HeapObject* object,
                           std::string* out,
                           std::string* error) {
  if (!out || !object || object->header.kind != ObjectKind::String) {
    if (error) *error = "external u8 conversion requires a managed string";
    return false;
  }
  const std::u16string source = ReadString(object);
  out->clear();
  out->reserve(source.size());
  for (char16_t value : source) {
    if (value == 0) {
      if (error) *error = "external u8 conversion rejects embedded NUL";
      return false;
    }
    if (value > 0xFFu) {
      if (error) *error = "managed string is not represented as UTF-8 bytes";
      return false;
    }
    out->push_back(static_cast<char>(value));
  }
  if (!IsValidUtf8(*out)) {
    if (error) *error = "managed string contains invalid UTF-8";
    return false;
  }
  return true;
}

} // namespace Simple::VM::Runtime
