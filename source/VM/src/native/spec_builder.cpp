#include "native/spec_builder.h"

#include <string>
#include <utility>

namespace Simple::VM::Native {

NativeFunctionSpec MakeSpec(Simple::Lang::LibraryModuleId module,
                            std::string_view symbol_name,
                            std::vector<Simple::Byte::TypeKind> params,
                            Simple::Byte::TypeKind result_type,
                            NativeFunctionHandler handler) {
  NativeFunctionSpec spec;
  spec.module_name = std::string(Simple::Lang::ToCanonicalName(module));
  spec.library_module = module;
  spec.symbol_name = std::string(symbol_name);
  spec.parameter_types = std::move(params);
  spec.result_type = result_type;
  spec.layer = NativeLayer::System;
  spec.stability = NativeStability::Experimental;
  spec.doc_summary = NativeFunctionName(spec.module_name, symbol_name);
  if (result_type == Simple::Byte::TypeKind::String || result_type == Simple::Byte::TypeKind::Ref) {
    spec.allocation = NativeAllocationBehavior::MayAllocateVm;
    spec.gc_behavior = NativeGcBehavior::MaySafepoint;
  }
  spec.handler = std::move(handler);
  return spec;
}

NativeOwnershipRule DefaultOwnershipForAccess(NativeResourceAccess access) {
  switch (access) {
    case NativeResourceAccess::Input:
      return NativeOwnershipRule::Borrow;
    case NativeResourceAccess::Output:
      return NativeOwnershipRule::TransferToCaller;
    case NativeResourceAccess::InputOutput:
      return NativeOwnershipRule::TransferToCallee;
  }
  return NativeOwnershipRule::None;
}

NativeCleanupBehavior DefaultCleanupForAccess(NativeResourceAccess access) {
  switch (access) {
    case NativeResourceAccess::Input:
      return NativeCleanupBehavior::None;
    case NativeResourceAccess::Output:
      return NativeCleanupBehavior::AutoCloseOnVmShutdown;
    case NativeResourceAccess::InputOutput:
      return NativeCleanupBehavior::CloseRequired;
  }
  return NativeCleanupBehavior::None;
}

NativeFunctionSpec WithResource(NativeFunctionSpec spec,
                                NativeResourceKind kind,
                                NativeResourceAccess access,
                                uint32_t parameter_index) {
  spec.resources.push_back(NativeResourceUse{kind, access, DefaultOwnershipForAccess(access),
                                             DefaultCleanupForAccess(access), parameter_index});
  return spec;
}

NativeFunctionSpec MayBlock(NativeFunctionSpec spec) {
  spec.blocking = NativeBlockingBehavior::MayBlock;
  return spec;
}

NativeFunctionSpec WithCapability(NativeFunctionSpec spec, const char* capability) {
  spec.capability_tags.push_back(capability);
  return spec;
}

NativeFunctionSpec WithStability(NativeFunctionSpec spec, NativeStability stability) {
  spec.stability = stability;
  return spec;
}

NativeFunctionSpec WithDoc(NativeFunctionSpec spec, const char* summary) {
  spec.doc_summary = summary;
  return spec;
}

} // namespace Simple::VM::Native
