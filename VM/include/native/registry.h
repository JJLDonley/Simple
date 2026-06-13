#ifndef SIMPLE_VM_NATIVE_REGISTRY_H
#define SIMPLE_VM_NATIVE_REGISTRY_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sbc_types.h"

namespace Simple::VM::Native {

using Slot = uint64_t;

struct NativeCallContext {
  std::vector<Slot> args;
};

struct NativeCallResult {
  bool ok = true;
  bool has_value = true;
  Slot value = 0;
  std::string error;
};

using NativeFunctionHandler = std::function<NativeCallResult(NativeCallContext&)>;

struct NativeFunctionSpec {
  std::string module_name;
  std::string symbol_name;
  std::vector<Simple::Byte::TypeKind> parameter_types;
  Simple::Byte::TypeKind result_type = Simple::Byte::TypeKind::Unspecified;
  NativeFunctionHandler handler;
};

struct NativeFunction {
  NativeFunctionSpec spec;
};

struct NativeModule {
  std::string name;
  std::vector<NativeFunction> functions;
};

class NativeRegistry {
 public:
  bool Register(NativeFunctionSpec spec);
  const NativeFunctionSpec* Find(const std::string& module_name,
                                 const std::string& symbol_name) const;
  size_t Size() const;

 private:
  std::vector<NativeFunctionSpec> functions_;
};

void RegisterSystemRandom(NativeRegistry& registry);
void RegisterSystemOs(NativeRegistry& registry);
void RegisterSystemThread(NativeRegistry& registry);
void RegisterSystemChannel(NativeRegistry& registry);
void RegisterSystemJson(NativeRegistry& registry);
void RegisterSystemLog(NativeRegistry& registry);
NativeRegistry BuildDefaultRegistry();

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_REGISTRY_H
