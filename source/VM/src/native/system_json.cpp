#include "native/registry.h"
#include "runtime/values.h"

#include "native/arg_utils.h"
#include "native/json.h"
#include "native/spec_builder.h"

#include <memory>
#include <string>
#include <utility>

namespace Simple::VM::Native {
using Simple::VM::Runtime::PackRef;

namespace {

Json::Document* GetJsonDocument(NativeCallContext& context, size_t index) {
  NativeResourceRecord* record = nullptr;
  if (context.ArgResourceHandle(index, NativeResourceKind::JsonValue, nullptr, &record) !=
          NativeResourceStatus::Ok ||
      !record) {
    return nullptr;
  }
  return static_cast<Json::Document*>(record->payload.get());
}

NativeCallResult JsonParse(NativeCallContext& context) {
  std::string text;
  if (!context.resource_registry || !ReadStringArg(context, 0, &text)) {
    return NativeCallResult::Handle({});
  }
  std::optional<Json::Document> document = Json::Parse(text);
  if (!document) return NativeCallResult::Handle({});

  auto owned_document = std::make_shared<Json::Document>(std::move(*document));
  NativeResourceRecord record;
  record.kind = NativeResourceKind::JsonValue;
  record.debug_label = "System.Json value";
  record.payload = owned_document;
  const NativeHandleId handle = context.resource_registry->Insert(std::move(record));
  if (handle.IsNull()) return NativeCallResult::Handle({});
  return NativeCallResult::Handle(handle);
}

NativeCallResult JsonStringify(NativeCallContext& context) {
  NativeCallResult result;
  Json::Document* document = GetJsonDocument(context, 0);
  if (!document || !Json::Stringify(*document, &result.string_value)) {
    result.value = PackRef(HeapLayout::kNullRef);
  }
  return result;
}

NativeCallResult JsonFree(NativeCallContext& context) {
  if (!context.resource_registry) return NativeCallResult::Bool(false);
  NativeHandleId handle;
  if (!context.ArgHandle(0, &handle)) return NativeCallResult::Bool(false);
  return NativeCallResult::Bool(
      context.resource_registry->Close(handle, NativeResourceKind::JsonValue, nullptr) ==
      NativeResourceStatus::Ok);
}

} // namespace

void RegisterSystemJson(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Json);
  registry.Register(WithResource(
      MayAllocateHost(MakeSpec(
          module, Simple::Lang::ToMember(Simple::Lang::SystemJsonMember::Parse),
          {TypeKind::String}, TypeKind::I64, JsonParse)),
      NativeResourceKind::JsonValue, NativeResourceAccess::Output));
  registry.Register(WithResource(
      MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemJsonMember::Stringify),
               {TypeKind::I64}, TypeKind::String, JsonStringify),
      NativeResourceKind::JsonValue, NativeResourceAccess::Input, 0));
  registry.Register(WithResource(
      MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemJsonMember::Free),
               {TypeKind::I64}, TypeKind::I32, JsonFree),
      NativeResourceKind::JsonValue, NativeResourceAccess::InputOutput, 0));
}

} // namespace Simple::VM::Native
