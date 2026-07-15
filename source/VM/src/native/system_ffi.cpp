#include "native/registry.h"
#include "runtime/values.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include "ffi/dl_runtime.h"

#include <memory>
#include <string>
#include <utility>

namespace Simple::VM::Native {
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::UnpackRef;

namespace {

struct DynamicLibraryResource {
  int64_t native_handle = 0;
};

bool CloseDynamicLibraryResource(void* payload, std::string* error) {
  auto* library = static_cast<DynamicLibraryResource*>(payload);
  if (!library || library->native_handle == 0) return true;
  if (!Simple::VM::Ffi::DlRuntime::Close(library->native_handle, error)) return false;
  library->native_handle = 0;
  return true;
}

void SetDlError(NativeCallContext& context, const std::string& text) {
  if (context.dl_last_error) *context.dl_last_error = text;
}

NativeResourceRecord* GetDynamicLibraryResource(NativeCallContext& context, size_t index) {
  NativeResourceRecord* record = nullptr;
  NativeHandleId handle;
  const NativeResourceStatus status = context.ArgResourceHandle(
      index, NativeResourceKind::FfiLibrary, &handle, &record);
  if (status != NativeResourceStatus::Ok) {
    SetDlError(context, "System.FFI invalid library handle: " +
                            std::string(NativeResourceStatusName(status)));
    return nullptr;
  }
  return record;
}

NativeCallResult DlOpen(NativeCallContext& context) {
  NativeCallResult result;
  if (!context.dl_last_error || !context.resource_registry) {
    SetDlError(context, "System.FFI.open runtime resource registry unavailable");
    result.value = PackI64(0);
    return result;
  }
  const uint32_t path_ref = UnpackRef(context.args[0]);
  if (path_ref == HeapLayout::kNullRef) {
    SetDlError(context, "System.FFI.open null path");
    result.value = PackI64(0);
    return result;
  }
  std::string path;
  if (!ReadStringArg(context, 0, &path)) {
    SetDlError(context, "System.FFI.open path not string");
    result.value = PackI64(0);
    return result;
  }
  const int64_t native_handle = Simple::VM::Ffi::DlRuntime::Open(path, context.dl_last_error);
  if (native_handle == 0) {
    result.value = PackI64(0);
    return result;
  }

  auto library = std::make_shared<DynamicLibraryResource>();
  library->native_handle = native_handle;
  NativeResourceRecord record;
  record.kind = NativeResourceKind::FfiLibrary;
  record.debug_label = path;
  record.payload = library;
  record.close = CloseDynamicLibraryResource;
  const NativeHandleId handle = context.resource_registry->Insert(std::move(record));
  if (handle.IsNull()) {
    CloseDynamicLibraryResource(library.get(), context.dl_last_error);
    SetDlError(context, "System.FFI.open resource registry is full");
    result.value = PackI64(0);
    return result;
  }
  return NativeCallResult::Handle(handle);
}

NativeCallResult DlSymbol(NativeCallContext& context) {
  NativeCallResult result;
  NativeResourceRecord* record = GetDynamicLibraryResource(context, 0);
  if (!record || !record->payload) {
    result.value = PackI64(0);
    return result;
  }
  const auto* library = static_cast<const DynamicLibraryResource*>(record->payload.get());
  const uint32_t name_ref = UnpackRef(context.args[1]);
  if (name_ref == HeapLayout::kNullRef) {
    SetDlError(context, "System.FFI.sym null name");
    result.value = PackI64(0);
    return result;
  }
  std::string name;
  if (!ReadStringArg(context, 1, &name)) {
    SetDlError(context, "System.FFI.sym name not string");
    result.value = PackI64(0);
    return result;
  }
  result.value = PackI64(context.dl_last_error
                             ? Simple::VM::Ffi::DlRuntime::Symbol(
                                   library->native_handle, name, context.dl_last_error)
                             : 0);
  return result;
}

NativeCallResult DlClose(NativeCallContext& context) {
  if (!context.resource_registry) {
    SetDlError(context, "System.FFI.close runtime resource registry unavailable");
    return NativeCallResult::I32(-1);
  }
  NativeHandleId handle;
  if (!context.ArgHandle(0, &handle)) {
    SetDlError(context, "System.FFI.close invalid handle encoding");
    return NativeCallResult::I32(-1);
  }
  const NativeResourceStatus status = context.resource_registry->Close(
      handle, NativeResourceKind::FfiLibrary, context.dl_last_error);
  if (status != NativeResourceStatus::Ok) {
    SetDlError(context, "System.FFI.close invalid library handle: " +
                            std::string(NativeResourceStatusName(status)));
    return NativeCallResult::I32(-1);
  }
  return NativeCallResult::I32(0);
}

NativeCallResult DlLastError(NativeCallContext& context) {
  NativeCallResult result;
  if (!context.dl_last_error || context.dl_last_error->empty()) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = *context.dl_last_error;
  return result;
}


} // namespace

void RegisterSystemDl(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::FFI);
  registry.Register(WithDoc(
      WithStability(
          WithResource(WithCapability(
                           MayBlock(MayAllocateHost(MakeSpec(
                               module,
                               Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::Open),
                               {TypeKind::String}, TypeKind::I64, DlOpen))),
                           "ffi.dynamic_load"),
                       NativeResourceKind::FfiLibrary, NativeResourceAccess::Output),
          NativeStability::Unsafe),
      "Open a dynamic library handle."));
  registry.Register(WithDoc(
      WithStability(
          WithResource(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::Sym),
                                               {TypeKind::I64, TypeKind::String}, TypeKind::Ptr,
                                               DlSymbol),
                                      "ffi.dynamic_load"),
                       NativeResourceKind::FfiLibrary, NativeResourceAccess::Input, 0),
          NativeStability::Unsafe),
      "Resolve a symbol from a dynamic library handle."));
  registry.Register(WithStability(
      WithResource(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::Close), {TypeKind::I64},
                                           TypeKind::I32, DlClose),
                                  "ffi.dynamic_load"),
                   NativeResourceKind::FfiLibrary, NativeResourceAccess::InputOutput, 0),
      NativeStability::Unsafe));
  registry.Register(WithStability(
      MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::LastError), {},
               TypeKind::String, DlLastError),
      NativeStability::Unsafe));
  registry.Register(WithStability(
      MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::LastErrorSnake), {},
               TypeKind::String, DlLastError),
      NativeStability::Unsafe));
}


} // namespace Simple::VM::Native
