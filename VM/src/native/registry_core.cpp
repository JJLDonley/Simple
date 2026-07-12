#include "native/registry.h"
#include "runtime/values.h"

#include <utility>

namespace Simple::VM::Native {
using Simple::VM::Runtime::PackI32;
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::PackF32;
using Simple::VM::Runtime::PackF64;


NativeCallResult NativeCallResult::Void() {
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult NativeCallResult::Bool(bool value) {
  NativeCallResult result;
  result.value = PackI32(value ? 1 : 0);
  return result;
}

NativeCallResult NativeCallResult::Char(uint32_t value) {
  NativeCallResult result;
  if (!Simple::VM::Runtime::IsValidAbiScalarValue(Simple::Byte::TypeKind::Char, value)) {
    result.ok = false;
    result.has_value = false;
    result.error = "invalid char ABI value";
    return result;
  }
  result.value = PackI32(static_cast<int32_t>(value));
  return result;
}

NativeCallResult NativeCallResult::I32(int32_t value) {
  NativeCallResult result;
  result.value = PackI32(value);
  return result;
}

NativeCallResult NativeCallResult::I64(int64_t value) {
  NativeCallResult result;
  result.value = PackI64(value);
  return result;
}

NativeCallResult NativeCallResult::F32(float value) {
  NativeCallResult result;
  result.value = PackF32(value);
  return result;
}

NativeCallResult NativeCallResult::F64(double value) {
  NativeCallResult result;
  result.value = PackF64(value);
  return result;
}

NativeCallResult NativeCallResult::Ref(uint32_t value) {
  NativeCallResult result;
  result.value = PackRef(value);
  return result;
}

NativeCallResult NativeCallResult::Handle(NativeHandleId value) {
  NativeCallResult result;
  result.value = PackNativeHandleId(value);
  return result;
}

NativeCallResult NativeCallResult::String(std::string value) {
  NativeCallResult result;
  result.string_value = std::move(value);
  return result;
}

NativeCallResult NativeCallResult::Error(std::string message) {
  NativeCallResult result;
  result.ok = false;
  result.has_value = false;
  result.error = std::move(message);
  return result;
}

bool NativeRegistry::Register(NativeFunctionSpec spec) {
  if (spec.module_name.empty() || spec.symbol_name.empty() || !spec.handler) return false;
  if (Find(spec.module_name, spec.symbol_name)) return false;
  functions_.push_back(std::move(spec));
  return true;
}

const NativeFunctionSpec* NativeRegistry::Find(const std::string& module_name,
                                               const std::string& symbol_name) const {
  for (const NativeFunctionSpec& spec : functions_) {
    if (spec.module_name == module_name && spec.symbol_name == symbol_name) return &spec;
  }
  return nullptr;
}

const NativeFunctionSpec* NativeRegistry::Find(Simple::Lang::LibraryModuleId module,
                                               const std::string& symbol_name) const {
  for (const NativeFunctionSpec& spec : functions_) {
    if (spec.library_module && *spec.library_module == module && spec.symbol_name == symbol_name) return &spec;
  }
  return nullptr;
}

const std::vector<NativeFunctionSpec>& NativeRegistry::Functions() const {
  return functions_;
}

size_t NativeRegistry::Size() const {
  return functions_.size();
}

} // namespace Simple::VM::Native
