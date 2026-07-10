#ifndef SIMPLE_VM_NATIVE_DISPATCH_H
#define SIMPLE_VM_NATIVE_DISPATCH_H

#include <cstdio>
#include <string>
#include <vector>

#include "heap.h"
#include "native/capability_policy.h"
#include "native/registry.h"
#include "sbc_types.h"

namespace Simple::VM::Native {

struct MetadataDispatchContext {
  Heap* heap = nullptr;
  const std::vector<std::string>* argv = nullptr;
  std::vector<std::FILE*>* open_files = nullptr;
  std::vector<NativeHandleId>* file_handles = nullptr;
  std::string* dl_last_error = nullptr;
  NativeResourceRegistry* resource_registry = nullptr;
  const CapabilityPolicy* capability_policy = nullptr;
};

bool DispatchMetadataImport(const NativeRegistry& registry,
                            const std::string& module_name,
                            const std::string& symbol_name,
                            const std::vector<Slot>& args,
                            Simple::Byte::TypeKind return_kind,
                            MetadataDispatchContext runtime,
                            Slot* out_ret,
                            bool* out_has_ret,
                            std::string* out_error);

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_DISPATCH_H
