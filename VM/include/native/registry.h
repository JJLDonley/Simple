#ifndef SIMPLE_VM_NATIVE_REGISTRY_H
#define SIMPLE_VM_NATIVE_REGISTRY_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "heap.h"
#include "native/resource_registry.h"
#include "runtime/abi.h"
#include "sbc_types.h"

namespace Simple::VM::Native {

using Slot = uint64_t;

struct NativeCallContext {
  std::vector<Slot> args;
  Heap* heap = nullptr;
  const std::vector<std::string>* argv = nullptr;
  std::vector<NativeHandleId>* file_handles = nullptr;
  std::string* dl_last_error = nullptr;
  NativeResourceRegistry* resource_registry = nullptr;
  std::vector<std::string> borrowed_string_storage;

  bool ArgBool(size_t index, bool* out) const;
  bool ArgChar(size_t index, uint32_t* out) const;
  bool ArgI32(size_t index, int32_t* out) const;
  bool ArgI64(size_t index, int64_t* out) const;
  bool ArgF32(size_t index, float* out) const;
  bool ArgF64(size_t index, double* out) const;
  bool ArgRef(size_t index, uint32_t* out) const;
  bool ArgHandle(size_t index, NativeHandleId* out) const;
  NativeResourceStatus ArgResourceHandle(size_t index,
                                         NativeResourceKind expected_kind,
                                         NativeHandleId* out_handle,
                                         NativeResourceRecord** out_record) const;
  bool ArgBytesView(size_t index, Simple::VM::Runtime::SimpleBytesView* out) const;
  bool ArgString(size_t index, std::string* out);
  bool ArgStringView(size_t index, Simple::VM::Runtime::SimpleStringView* out);
};

struct NativeCallResult {
  bool ok = true;
  bool has_value = true;
  Slot value = 0;
  std::string string_value;
  std::string error;

  static NativeCallResult Void();
  static NativeCallResult Bool(bool value);
  static NativeCallResult Char(uint32_t value);
  static NativeCallResult I32(int32_t value);
  static NativeCallResult I64(int64_t value);
  static NativeCallResult F32(float value);
  static NativeCallResult F64(double value);
  static NativeCallResult Ref(uint32_t value);
  static NativeCallResult Handle(NativeHandleId value);
  static NativeCallResult String(std::string value);
  static NativeCallResult Error(std::string message);
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

enum class NativeAllocationBehavior {
  NoAllocation,
  MayAllocateVm,
  MayAllocateHost,
};

enum class NativeGcBehavior {
  NoSafepoint,
  MaySafepoint,
};

enum class NativeStability {
  Experimental,
  Stable,
  Unsafe,
};

enum class NativeOwnershipRule {
  None,
  Borrow,
  TransferToCaller,
  TransferToCallee,
};

enum class NativeCleanupBehavior {
  None,
  CloseRequired,
  AutoCloseOnVmShutdown,
};

struct NativeResourceUse {
  NativeResourceKind kind = NativeResourceKind::Unknown;
  NativeResourceAccess access = NativeResourceAccess::Input;
  NativeOwnershipRule ownership = NativeOwnershipRule::None;
  NativeCleanupBehavior cleanup = NativeCleanupBehavior::None;
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
  NativeAllocationBehavior allocation = NativeAllocationBehavior::NoAllocation;
  NativeGcBehavior gc_behavior = NativeGcBehavior::NoSafepoint;
  bool direct_binding_safe = false;
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
bool ValidateNativeRegistryMetadata(const NativeRegistry& registry, std::string* error);
bool IsDirectNativeBindingSafe(const NativeFunctionSpec& spec);
std::string GenerateStdLibMarkdown(const NativeRegistry& registry);

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_REGISTRY_H
