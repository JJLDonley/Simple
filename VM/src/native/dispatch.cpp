#include "native/dispatch.h"

#include <cstring>

namespace Simple::VM::Native {
namespace {

constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

inline Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

std::u16string AsciiToU16(const std::string& text) {
  std::u16string out;
  out.reserve(text.size());
  for (unsigned char c : text) out.push_back(static_cast<char16_t>(c));
  return out;
}

void WriteU32(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  if (offset + sizeof(value) > payload.size()) return;
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint32_t CreateString(Heap& heap, const std::u16string& text) {
  const uint32_t len = static_cast<uint32_t>(text.size());
  const uint32_t handle = heap.Allocate(ObjectKind::String, 0,
                                        static_cast<uint32_t>(HeapLayout::StringPayloadSize(len)));
  HeapObject* obj = heap.Get(handle);
  if (!obj) return kNullRef;
  WriteU32(obj->payload, HeapLayout::kStringLengthOffset, len);
  for (uint32_t i = 0; i < len; ++i) {
    const uint16_t cu = static_cast<uint16_t>(text[i]);
    const size_t off = HeapLayout::StringCodeUnitOffset(i);
    if (off + 1 >= obj->payload.size()) break;
    obj->payload[off] = static_cast<uint8_t>(cu & 0xFFu);
    obj->payload[off + 1] = static_cast<uint8_t>((cu >> 8) & 0xFFu);
  }
  return handle;
}

bool IsI32LikeImportType(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::Bool:
    case TypeKind::Char:
      return true;
    default:
      return false;
  }
}

bool IsI64LikeImportType(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  return kind == TypeKind::I64 || kind == TypeKind::U64;
}

bool IsStringLikeImportType(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  return kind == TypeKind::String || kind == TypeKind::Ref;
}

bool IsCompatibleNativeReturnType(Simple::Byte::TypeKind expected, Simple::Byte::TypeKind actual) {
  using Simple::Byte::TypeKind;
  switch (expected) {
    case TypeKind::Unspecified:
      return true;
    case TypeKind::I32:
    case TypeKind::U32:
    case TypeKind::I16:
    case TypeKind::U16:
    case TypeKind::I8:
    case TypeKind::U8:
    case TypeKind::Bool:
    case TypeKind::Char:
      return IsI32LikeImportType(actual);
    case TypeKind::I64:
    case TypeKind::U64:
      return IsI64LikeImportType(actual);
    case TypeKind::String:
      return IsStringLikeImportType(actual);
    case TypeKind::Ref:
      return actual == TypeKind::Ref;
    case TypeKind::F32:
    case TypeKind::F64:
      return actual == expected;
    default:
      return actual == expected;
  }
}

} // namespace

bool DispatchMetadataImport(const NativeRegistry& registry,
                            const std::string& module_name,
                            const std::string& symbol_name,
                            const std::vector<Slot>& args,
                            Simple::Byte::TypeKind return_kind,
                            MetadataDispatchContext runtime,
                            Slot* out_ret,
                            bool* out_has_ret,
                            std::string* out_error) {
  const NativeFunctionSpec* spec = registry.Find(module_name, symbol_name);
  if (!spec) return false;
  if (!out_ret || !out_has_ret || !runtime.heap) {
    if (out_error) *out_error = module_name + "." + symbol_name + " native dispatch context invalid";
    return true;
  }
  if (spec->result_type == Simple::Byte::TypeKind::Unspecified) {
    *out_has_ret = false;
  } else if (!IsCompatibleNativeReturnType(spec->result_type, return_kind)) {
    if (out_error) *out_error = module_name + "." + symbol_name + " return type mismatch";
    return true;
  }
  if (args.size() != spec->parameter_types.size()) {
    if (out_error) *out_error = module_name + "." + symbol_name + " arg count mismatch";
    return true;
  }

  NativeCallContext context;
  context.args = args;
  context.heap = runtime.heap;
  context.argv = runtime.argv;
  context.open_files = runtime.open_files;
  context.dl_last_error = runtime.dl_last_error;
  NativeCallResult result = spec->handler(context);
  if (!result.ok) {
    if (out_error) *out_error = result.error;
    return true;
  }
  if (spec->result_type == Simple::Byte::TypeKind::String) {
    *out_ret = result.string_value.empty() && result.value == PackRef(kNullRef)
                   ? PackRef(kNullRef)
                   : PackRef(CreateString(*runtime.heap, AsciiToU16(result.string_value)));
  } else if (result.has_value) {
    *out_ret = result.value;
  }
  return true;
}

} // namespace Simple::VM::Native
