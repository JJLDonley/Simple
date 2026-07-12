#ifndef SIMPLE_VM_NATIVE_SPEC_BUILDER_H
#define SIMPLE_VM_NATIVE_SPEC_BUILDER_H

#include <cstdint>
#include <string_view>
#include <vector>

#include "library_catalog.h"
#include "native/registry.h"
#include "sbc_types.h"

namespace Simple::VM::Native {

NativeFunctionSpec MakeSpec(Simple::Lang::LibraryModuleId module,
                            std::string_view symbol_name,
                            std::vector<Simple::Byte::TypeKind> params,
                            Simple::Byte::TypeKind result_type,
                            NativeFunctionHandler handler);

NativeOwnershipRule DefaultOwnershipForAccess(NativeResourceAccess access);
NativeCleanupBehavior DefaultCleanupForAccess(NativeResourceAccess access);

NativeFunctionSpec WithResource(NativeFunctionSpec spec,
                                NativeResourceKind kind,
                                NativeResourceAccess access,
                                uint32_t parameter_index = 0xffffffffu);
NativeFunctionSpec MayBlock(NativeFunctionSpec spec);
NativeFunctionSpec WithCapability(NativeFunctionSpec spec, const char* capability);
NativeFunctionSpec WithStability(NativeFunctionSpec spec, NativeStability stability);
NativeFunctionSpec WithDoc(NativeFunctionSpec spec, const char* summary);

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_SPEC_BUILDER_H
