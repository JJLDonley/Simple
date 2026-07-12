#include "native/registry.h"
#include "native/slot_codec.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include "ffi/dl_runtime.h"

#include <string>

namespace Simple::VM::Native {
namespace {

void SetDlError(NativeCallContext& context, const std::string& text) {
  if (context.dl_last_error) *context.dl_last_error = text;
}

NativeCallResult DlOpen(NativeCallContext& context) {
  NativeCallResult result;
  if (!context.dl_last_error) {
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
  result.value = PackI64(Simple::VM::Ffi::DlRuntime::Open(path, context.dl_last_error));
  return result;
}

NativeCallResult DlSymbol(NativeCallContext& context) {
  NativeCallResult result;
  const int64_t handle = UnpackI64(context.args[0]);
  if (handle == 0) {
    SetDlError(context, "System.FFI.sym null handle");
    result.value = PackI64(0);
    return result;
  }
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
  result.value = PackI64(context.dl_last_error ? Simple::VM::Ffi::DlRuntime::Symbol(handle, name, context.dl_last_error) : 0);
  return result;
}

NativeCallResult DlClose(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Simple::VM::Ffi::DlRuntime::Close(UnpackI64(context.args[0]), context.dl_last_error) ? 0 : -1);
  return result;
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
          WithResource(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::Open), {TypeKind::String},
                                               TypeKind::I64, DlOpen),
                                      "ffi.dynamic_load"),
                       NativeResourceKind::FfiLibrary, NativeResourceAccess::Output),
          NativeStability::Unsafe),
      "Open a dynamic library handle."));
  registry.Register(WithDoc(
      WithStability(
          WithResource(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::Sym),
                                               {TypeKind::I64, TypeKind::String}, TypeKind::I64,
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
  registry.Register(WithStability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFFIMember::LastErrorSnake), {}, TypeKind::String,
                                           DlLastError),
                                  NativeStability::Unsafe));
}


} // namespace Simple::VM::Native
