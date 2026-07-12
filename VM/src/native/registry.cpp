#include "native/registry.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include "ffi/dl_runtime.h"
#include "native/json.h"
#include "native/log.h"

namespace Simple::VM::Native {
namespace {

Slot PackI32(int32_t value) {
  return static_cast<uint32_t>(value);
}

int32_t UnpackI32(Slot value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
}

uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value & 0xFFFFFFFFu);
}

Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}


NativeCallResult JsonParse(NativeCallContext& context) {
  NativeCallResult result;
  std::string text;
  result.value = PackI64(ReadStringArg(context, 0, &text) ? Json::Parse(text) : 0);
  return result;
}

NativeCallResult JsonStringify(NativeCallContext& context) {
  NativeCallResult result;
  if (!Json::Stringify(UnpackI64(context.args[0]), &result.string_value)) {
    result.value = PackRef(HeapLayout::kNullRef);
  }
  return result;
}

NativeCallResult JsonFree(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Json::Free(UnpackI64(context.args[0])) ? 1 : 0);
  return result;
}

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

NativeCallResult LogSetLevel(NativeCallContext& context) {
  Log::SetLevel(UnpackI32(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult LogSetFile(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  result.value = PackI32(ReadStringArg(context, 0, &path) && Log::SetFile(path) ? 1 : 0);
  return result;
}

NativeCallResult LogEmit(NativeCallContext& context) {
  NativeCallResult result;
  std::string message;
  if (ReadStringArg(context, 1, &message)) Log::Emit(message, UnpackI32(context.args[0]));
  result.has_value = false;
  return result;
}

NativeCallResult LogFlush(NativeCallContext&) {
  return NativeCallResult::Bool(Log::Flush());
}

NativeCallResult LogInfo(NativeCallContext& context) {
  NativeCallResult result;
  std::string message;
  if (ReadStringArg(context, 0, &message)) Log::Emit(message, 1);
  result.has_value = false;
  return result;
}

NativeCallResult LogWarn(NativeCallContext& context) {
  NativeCallResult result;
  std::string message;
  if (ReadStringArg(context, 0, &message)) Log::Emit(message, 2);
  result.has_value = false;
  return result;
}

NativeCallResult LogError(NativeCallContext& context) {
  NativeCallResult result;
  std::string message;
  if (ReadStringArg(context, 0, &message)) Log::Emit(message, 3);
  result.has_value = false;
  return result;
}


} // namespace

void RegisterSystemJson(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Json);
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemJsonMember::Parse), {TypeKind::String}, TypeKind::I64,
                             JsonParse));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemJsonMember::Stringify), {TypeKind::I64}, TypeKind::String,
                             JsonStringify));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemJsonMember::Free), {TypeKind::I64}, TypeKind::I32, JsonFree));
}

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

void RegisterSystemLog(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Log);
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::SetLevel), {TypeKind::I32}, TypeKind::Unspecified,
                             LogSetLevel));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::SetFile), {TypeKind::String}, TypeKind::I32,
                             LogSetFile));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::Log), {TypeKind::I32, TypeKind::String},
                             TypeKind::Unspecified, LogEmit));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::Flush), {}, TypeKind::Bool, LogFlush));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::Info), {TypeKind::String}, TypeKind::Unspecified,
                             LogInfo));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::Warn), {TypeKind::String}, TypeKind::Unspecified,
                             LogWarn));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemLogMember::Error), {TypeKind::String}, TypeKind::Unspecified,
                             LogError));
}


} // namespace Simple::VM::Native
