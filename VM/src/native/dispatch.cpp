#include "native/dispatch.h"

#include <cstring>

#include "native/capability_policy.h"

namespace Simple::VM::Native {
namespace {

constexpr uint32_t kNullRef = Simple::VM::HeapLayout::kNullRef;

inline Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
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
  if (runtime.capability_policy) {
    std::string denied_tag;
    if (!AllowsCapabilities(*runtime.capability_policy, spec->capability_tags, &denied_tag)) {
      if (out_error) {
        *out_error = module_name + "." + symbol_name + " denied capability: " + denied_tag;
      }
      return true;
    }
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
                   : PackRef(Simple::VM::CreateString(*runtime.heap, Simple::VM::AsciiToU16(result.string_value)));
  } else if (result.has_value) {
    *out_ret = result.value;
  }
  return true;
}

} // namespace Simple::VM::Native
