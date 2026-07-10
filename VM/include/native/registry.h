#ifndef SIMPLE_VM_NATIVE_REGISTRY_H
#define SIMPLE_VM_NATIVE_REGISTRY_H

#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "heap.h"
#include "native/resource_registry.h"
#include "sbc_types.h"

namespace Simple::VM::Native {

using Slot = uint64_t;

struct NativeCallContext {
  std::vector<Slot> args;
  Heap* heap = nullptr;
  const std::vector<std::string>* argv = nullptr;
  std::vector<std::FILE*>* open_files = nullptr;
  std::string* dl_last_error = nullptr;
};

struct NativeCallResult {
  bool ok = true;
  bool has_value = true;
  Slot value = 0;
  std::string string_value;
  std::string error;
};

using NativeFunctionHandler = std::function<NativeCallResult(NativeCallContext&)>;

enum class NativeLayer {
  Core,
  System,
  Standard,
  Domain,
};

enum class NativeResourceAccess {
  Input,
  Output,
  InputOutput,
};

enum class NativeBlockingBehavior {
  NonBlocking,
  MayBlock,
};

enum class NativeStability {
  Experimental,
  Stable,
  Unsafe,
};

struct NativeResourceUse {
  NativeResourceKind kind = NativeResourceKind::Unknown;
  NativeResourceAccess access = NativeResourceAccess::Input;
  uint32_t parameter_index = 0xffffffffu;
};

struct NativeFunctionSpec {
  std::string module_name;
  std::string symbol_name;
  std::vector<Simple::Byte::TypeKind> parameter_types;
  Simple::Byte::TypeKind result_type = Simple::Byte::TypeKind::Unspecified;
  NativeLayer layer = NativeLayer::System;
  std::vector<NativeResourceUse> resources;
  NativeBlockingBehavior blocking = NativeBlockingBehavior::NonBlocking;
  std::vector<std::string> capability_tags;
  std::vector<std::string> platforms;
  NativeStability stability = NativeStability::Experimental;
  std::string doc_summary;
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
  const std::vector<NativeFunctionSpec>& Functions() const;
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
void RegisterSystemBuffer(NativeRegistry& registry);
void RegisterSystemEnv(NativeRegistry& registry);
void RegisterSystemPath(NativeRegistry& registry);
void RegisterSystemFs(NativeRegistry& registry);
void RegisterSystemIo(NativeRegistry& registry);
void RegisterSystemDl(NativeRegistry& registry);
NativeRegistry BuildDefaultRegistry();
std::string GenerateStdLibMarkdown(const NativeRegistry& registry);

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_REGISTRY_H
