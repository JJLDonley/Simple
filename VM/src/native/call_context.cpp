#include "native/registry.h"
#include "native/slot_codec.h"

#include "native/arg_utils.h"
#include "runtime/abi.h"

#include <string>
#include <utility>

namespace Simple::VM::Native {

bool NativeCallContext::ArgBool(size_t index, bool* out) const {
  if (!out || index >= args.size()) return false;
  const uint64_t value = static_cast<uint64_t>(UnpackI32(args[index]));
  if (!Simple::VM::Runtime::IsValidAbiScalarValue(Simple::Byte::TypeKind::Bool, value)) {
    return false;
  }
  *out = value != 0;
  return true;
}

bool NativeCallContext::ArgChar(size_t index, uint32_t* out) const {
  if (!out || index >= args.size()) return false;
  const uint64_t value = static_cast<uint64_t>(UnpackI32(args[index]));
  if (!Simple::VM::Runtime::IsValidAbiScalarValue(Simple::Byte::TypeKind::Char, value)) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

bool NativeCallContext::ArgI32(size_t index, int32_t* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackI32(args[index]);
  return true;
}

bool NativeCallContext::ArgI64(size_t index, int64_t* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackI64(args[index]);
  return true;
}

bool NativeCallContext::ArgF32(size_t index, float* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackF32(UnpackU32Bits(args[index]));
  return true;
}

bool NativeCallContext::ArgF64(size_t index, double* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackF64(UnpackU64Bits(args[index]));
  return true;
}

bool NativeCallContext::ArgRef(size_t index, uint32_t* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackRef(args[index]);
  return true;
}

bool NativeCallContext::ArgHandle(size_t index, NativeHandleId* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackNativeHandleId(args[index]);
  return true;
}

NativeResourceStatus NativeCallContext::ArgResourceHandle(size_t index,
                                                         NativeResourceKind expected_kind,
                                                         NativeHandleId* out_handle,
                                                         NativeResourceRecord** out_record) const {
  if (out_handle) *out_handle = NativeHandleId{};
  if (out_record) *out_record = nullptr;
  if (!resource_registry || index >= args.size()) return NativeResourceStatus::InvalidHandle;
  const NativeHandleId handle = UnpackNativeHandleId(args[index]);
  if (out_handle) *out_handle = handle;
  return resource_registry->Get(handle, expected_kind, out_record);
}

bool NativeCallContext::ArgBytesView(size_t index, Simple::VM::Runtime::SimpleBytesView* out) const {
  if (!out || !heap || index >= args.size()) return false;
  const uint32_t ref = UnpackRef(args[index]);
  const HeapObject* obj = heap->Get(ref);
  if (!obj || obj->header.kind != ObjectKind::Bytes ||
      obj->payload.size() < HeapLayout::kBytesDataOffset) {
    return false;
  }
  const uint32_t length = ReadU32Payload(obj->payload, HeapLayout::kBytesLengthOffset);
  if (HeapLayout::kBytesDataOffset + static_cast<size_t>(length) > obj->payload.size()) {
    return false;
  }
  out->data = length == 0 ? nullptr : obj->payload.data() + HeapLayout::kBytesDataOffset;
  out->size = length;
  return true;
}

bool NativeCallContext::ArgString(size_t index, std::string* out) {
  return ReadStringArg(*this, index, out);
}

bool NativeCallContext::ArgStringView(size_t index, Simple::VM::Runtime::SimpleStringView* out) {
  if (!out) return false;
  std::string value;
  if (!ArgString(index, &value)) return false;
  borrowed_string_storage.push_back(std::move(value));
  const std::string& stored = borrowed_string_storage.back();
  out->data = stored.empty() ? nullptr : stored.data();
  out->size = stored.size();
  out->encoding = Simple::VM::Runtime::AbiStringEncoding::Utf8;
  return true;
}

} // namespace Simple::VM::Native
