#include "native/registry.h"
#include "native/slot_codec.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include "native/log.h"

#include <string>

namespace Simple::VM::Native {
namespace {

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
