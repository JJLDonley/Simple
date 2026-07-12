#include "interpreter/globals.h"

#include "runtime/values.h"

#include <string>
#include <vector>

namespace Simple::VM::Interpreter {
namespace {

using Simple::VM::Runtime::PackRef;

constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

} // namespace

bool LoadConstStringSlot(const Simple::Byte::SbcModule& module, Heap& heap, uint32_t const_id, Slot& out_value) {
  if (const_id + 8 > module.const_pool.size()) return false;
  const uint32_t kind = Simple::VM::ReadU32Payload(module.const_pool, const_id);
  if (kind != 0) return false;
  const uint32_t str_offset = Simple::VM::ReadU32Payload(module.const_pool, const_id + 4);
  if (str_offset >= module.const_pool.size()) return false;
  const char* base = reinterpret_cast<const char*>(module.const_pool.data() + str_offset);
  std::u16string text;
  for (size_t i = 0; str_offset + i < module.const_pool.size(); ++i) {
    const char c = base[i];
    if (c == '\0') break;
    text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  }
  const uint32_t handle = Simple::VM::CreateString(heap, text);
  if (handle == kNullRef) return false;
  out_value = PackRef(handle);
  return true;
}

bool IsRefLikeGlobal(const Simple::Byte::SbcModule& module, size_t global_index) {
  if (global_index >= module.globals.size()) return false;
  const uint32_t type_id = module.globals[global_index].type_id;
  if (type_id >= module.types.size()) return false;
  const auto& row = module.types[type_id];
  const auto kind = static_cast<Simple::Byte::TypeKind>(row.kind);
  if (kind == Simple::Byte::TypeKind::Ref || kind == Simple::Byte::TypeKind::String) return true;
  return kind == Simple::Byte::TypeKind::Unspecified && (row.flags & 0x1u) != 0u;
}

} // namespace Simple::VM::Interpreter
