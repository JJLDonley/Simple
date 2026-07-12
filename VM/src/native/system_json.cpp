#include "native/registry.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include "native/json.h"

namespace Simple::VM::Native {
namespace {

Slot PackI32(int32_t value) {
  return static_cast<uint32_t>(value);
}

int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
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


} // namespace Simple::VM::Native
