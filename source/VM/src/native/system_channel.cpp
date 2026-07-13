#include "native/registry.h"
#include "runtime/values.h"

#include "native/arg_utils.h"
#include "native/channel.h"
#include "native/spec_builder.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Simple::VM::Native {
using Simple::VM::Runtime::PackI32;
using Simple::VM::Runtime::PackI64;
using Simple::VM::Runtime::PackRef;
using Simple::VM::Runtime::PackF32;
using Simple::VM::Runtime::PackF64;
using Simple::VM::Runtime::UnpackI32;
using Simple::VM::Runtime::UnpackI64;
using Simple::VM::Runtime::UnpackF32;
using Simple::VM::Runtime::UnpackF64;

namespace {

struct ChannelResource {
  int64_t handle = 0;
};

bool CloseChannelResource(void* payload, std::string*) {
  auto* channel = static_cast<ChannelResource*>(payload);
  if (!channel || channel->handle == 0) return true;
  Channel::DestroyAll(channel->handle);
  channel->handle = 0;
  return true;
}

NativeCallResult CreateChannelResource(NativeCallContext& context, int64_t raw_handle) {
  if (!context.resource_registry || raw_handle == 0) {
    Channel::DestroyAll(raw_handle);
    return NativeCallResult::Handle({});
  }
  auto channel = std::make_shared<ChannelResource>();
  channel->handle = raw_handle;
  NativeResourceRecord record;
  record.kind = NativeResourceKind::Channel;
  record.debug_label = "System.Channel";
  record.payload = channel;
  record.close = CloseChannelResource;
  const NativeHandleId handle = context.resource_registry->Insert(std::move(record));
  if (handle.IsNull()) {
    Channel::DestroyAll(raw_handle);
    return NativeCallResult::Handle({});
  }
  return NativeCallResult::Handle(handle);
}

int64_t GetChannelHandle(NativeCallContext& context) {
  NativeResourceRecord* record = nullptr;
  if (context.ArgResourceHandle(0, NativeResourceKind::Channel, nullptr, &record) !=
          NativeResourceStatus::Ok ||
      !record || !record->payload) {
    return 0;
  }
  return static_cast<ChannelResource*>(record->payload.get())->handle;
}

NativeCallResult ChannelNewI32(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_i32));
}

NativeCallResult ChannelSendI32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_i32, GetChannelHandle(context),
                                       UnpackI32(context.args[1]))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvI32(NativeCallContext& context) {
  int32_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i32, GetChannelHandle(context), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value);
  return result;
}

NativeCallResult ChannelTryRecvI32(NativeCallContext& context) {
  int32_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i32, GetChannelHandle(context), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value);
  return result;
}

NativeCallResult ChannelPendingI32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_i32, GetChannelHandle(context)));
  return result;
}

NativeCallResult ChannelNewI64(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_i64));
}

NativeCallResult ChannelSendI64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_i64, GetChannelHandle(context),
                                       UnpackI64(context.args[1]))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvI64(NativeCallContext& context) {
  int64_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i64, GetChannelHandle(context), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI64(value);
  return result;
}

NativeCallResult ChannelTryRecvI64(NativeCallContext& context) {
  int64_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i64, GetChannelHandle(context), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI64(value);
  return result;
}

NativeCallResult ChannelPendingI64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_i64, GetChannelHandle(context)));
  return result;
}

NativeCallResult ChannelNewBool(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_bool));
}

NativeCallResult ChannelSendBool(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_bool, GetChannelHandle(context),
                                       UnpackI32(context.args[1]) != 0)
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvBool(NativeCallContext& context) {
  bool value = false;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_bool, GetChannelHandle(context), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value ? 1 : 0);
  return result;
}

NativeCallResult ChannelTryRecvBool(NativeCallContext& context) {
  bool value = false;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_bool, GetChannelHandle(context), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value ? 1 : 0);
  return result;
}

NativeCallResult ChannelPendingBool(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_bool, GetChannelHandle(context)));
  return result;
}

NativeCallResult ChannelNewF32(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_f32));
}

NativeCallResult ChannelSendF32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_f32, GetChannelHandle(context),
                                       UnpackF32(context.args[1]))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvF32(NativeCallContext& context) {
  float value = 0.0f;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f32, GetChannelHandle(context), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF32(value);
  return result;
}

NativeCallResult ChannelTryRecvF32(NativeCallContext& context) {
  float value = 0.0f;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f32, GetChannelHandle(context), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF32(value);
  return result;
}

NativeCallResult ChannelPendingF32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_f32, GetChannelHandle(context)));
  return result;
}

NativeCallResult ChannelNewF64(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_f64));
}

NativeCallResult ChannelSendF64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_f64, GetChannelHandle(context),
                                       UnpackF64(context.args[1]))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvF64(NativeCallContext& context) {
  double value = 0.0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f64, GetChannelHandle(context), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF64(value);
  return result;
}

NativeCallResult ChannelTryRecvF64(NativeCallContext& context) {
  double value = 0.0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f64, GetChannelHandle(context), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF64(value);
  return result;
}

NativeCallResult ChannelPendingF64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_f64, GetChannelHandle(context)));
  return result;
}

NativeCallResult ChannelNewString(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_string));
}

NativeCallResult ChannelPendingString(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_string, GetChannelHandle(context)));
  return result;
}

NativeCallResult ChannelNewBytes(NativeCallContext& context) {
  return CreateChannelResource(context, Channel::New(Channel::g_bytes));
}

NativeCallResult ChannelPendingBytes(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_bytes, GetChannelHandle(context)));
  return result;
}

std::u16string AsciiToU16Local(const std::string& value) {
  std::u16string out;
  out.reserve(value.size());
  for (unsigned char ch : value) out.push_back(static_cast<char16_t>(ch));
  return out;
}

std::string U16ToAsciiLocal(const std::u16string& value) {
  std::string out;
  out.reserve(value.size());
  for (char16_t ch : value) out.push_back(ch <= 0x7fu ? static_cast<char>(ch) : '?');
  return out;
}

NativeCallResult ChannelSendString(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  result.value = PackI32(ReadStringArg(context, 1, &value) &&
                                 Channel::Send(Channel::g_string, GetChannelHandle(context),
                                               AsciiToU16Local(value))
                             ? 1
                             : 0);
  return result;
}

NativeCallResult ChannelTrySendString(NativeCallContext& context) {
  return ChannelSendString(context);
}

NativeCallResult ChannelSendBytes(NativeCallContext& context) {
  NativeCallResult result;
  std::vector<int32_t> values;
  result.value = PackI32(ReadByteSequence(context, 1, &values) &&
                                 Channel::Send(Channel::g_bytes, GetChannelHandle(context), values)
                             ? 1
                             : 0);
  return result;
}

NativeCallResult ChannelTrySendBytes(NativeCallContext& context) {
  return ChannelSendBytes(context);
}

NativeCallResult ChannelRecvStringImpl(NativeCallContext& context, bool wait) {
  NativeCallResult result;
  std::u16string value;
  if (!Channel::Receive(Channel::g_string, GetChannelHandle(context), wait, &value)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = U16ToAsciiLocal(value);
  return result;
}

NativeCallResult ChannelRecvString(NativeCallContext& context) {
  return ChannelRecvStringImpl(context, true);
}

NativeCallResult ChannelTryRecvString(NativeCallContext& context) {
  return ChannelRecvStringImpl(context, false);
}

NativeCallResult ChannelRecvBytesImpl(NativeCallContext& context, bool wait) {
  NativeCallResult result;
  std::vector<int32_t> values;
  if (!context.heap || !Channel::Receive(Channel::g_bytes, GetChannelHandle(context), wait, &values)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  std::vector<uint32_t> bytes;
  bytes.reserve(values.size());
  for (int32_t value : values) bytes.push_back(static_cast<uint32_t>(value));
  result.value = CreateByteList(*context.heap, bytes);
  return result;
}

NativeCallResult ChannelRecvBytes(NativeCallContext& context) {
  return ChannelRecvBytesImpl(context, true);
}

NativeCallResult ChannelTryRecvBytes(NativeCallContext& context) {
  return ChannelRecvBytesImpl(context, false);
}

NativeCallResult ChannelClose(NativeCallContext& context) {
  if (!context.resource_registry) {
    return NativeCallResult::Error("System.Channel.close resource registry unavailable");
  }
  NativeHandleId handle;
  if (!context.ArgHandle(0, &handle)) {
    return NativeCallResult::Error("System.Channel.close invalid handle encoding");
  }
  const NativeResourceStatus status = context.resource_registry->Close(
      handle, NativeResourceKind::Channel, nullptr);
  if (status != NativeResourceStatus::Ok) {
    return NativeCallResult::Error("System.Channel.close invalid resource handle: " +
                                   std::string(NativeResourceStatusName(status)));
  }
  return NativeCallResult::Void();
}


} // namespace

void RegisterSystemChannel(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Channel);

  auto register_family = [&](const char* suffix,
                             TypeKind value_type,
                             NativeFunctionHandler new_handler,
                             NativeFunctionHandler send_handler,
                             NativeFunctionHandler try_send_handler,
                             NativeFunctionHandler recv_handler,
                             NativeFunctionHandler try_recv_handler,
                             NativeFunctionHandler pending_handler) {
    const std::string suffix_text(suffix);
    registry.Register(WithResource(
        MayAllocateHost(MakeSpec(module, (std::string("new") + suffix_text).c_str(), {},
                                 TypeKind::I64, std::move(new_handler))),
        NativeResourceKind::Channel, NativeResourceAccess::Output));
    registry.Register(WithResource(
        MakeSpec(module, ("send" + suffix_text).c_str(), {TypeKind::I64, value_type},
                 TypeKind::I32, std::move(send_handler)),
        NativeResourceKind::Channel, NativeResourceAccess::Input, 0));
    registry.Register(WithResource(
        MakeSpec(module, ("trySend" + suffix_text).c_str(), {TypeKind::I64, value_type},
                 TypeKind::I32, std::move(try_send_handler)),
        NativeResourceKind::Channel, NativeResourceAccess::Input, 0));
    registry.Register(WithResource(
        MayBlock(MakeSpec(module, ("recv" + suffix_text).c_str(), {TypeKind::I64},
                          value_type, std::move(recv_handler))),
        NativeResourceKind::Channel, NativeResourceAccess::Input, 0));
    registry.Register(WithResource(
        MakeSpec(module, ("tryRecv" + suffix_text).c_str(), {TypeKind::I64}, value_type,
                 std::move(try_recv_handler)),
        NativeResourceKind::Channel, NativeResourceAccess::Input, 0));
    registry.Register(WithResource(
        MakeSpec(module, ("pending" + suffix_text).c_str(), {TypeKind::I64}, TypeKind::I32,
                 std::move(pending_handler)),
        NativeResourceKind::Channel, NativeResourceAccess::Input, 0));
  };

  register_family("I32", TypeKind::I32, ChannelNewI32, ChannelSendI32, ChannelSendI32,
                  ChannelRecvI32, ChannelTryRecvI32, ChannelPendingI32);
  register_family("I64", TypeKind::I64, ChannelNewI64, ChannelSendI64, ChannelSendI64,
                  ChannelRecvI64, ChannelTryRecvI64, ChannelPendingI64);
  register_family("F32", TypeKind::F32, ChannelNewF32, ChannelSendF32, ChannelSendF32,
                  ChannelRecvF32, ChannelTryRecvF32, ChannelPendingF32);
  register_family("F64", TypeKind::F64, ChannelNewF64, ChannelSendF64, ChannelSendF64,
                  ChannelRecvF64, ChannelTryRecvF64, ChannelPendingF64);
  register_family("Bool", TypeKind::Bool, ChannelNewBool, ChannelSendBool, ChannelSendBool,
                  ChannelRecvBool, ChannelTryRecvBool, ChannelPendingBool);
  register_family("String", TypeKind::String, ChannelNewString, ChannelSendString,
                  ChannelTrySendString, ChannelRecvString, ChannelTryRecvString,
                  ChannelPendingString);
  register_family("Bytes", TypeKind::Ref, ChannelNewBytes, ChannelSendBytes, ChannelTrySendBytes,
                  ChannelRecvBytes, ChannelTryRecvBytes, ChannelPendingBytes);

  registry.Register(WithResource(
      MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemChannelMember::Close),
               {TypeKind::I64}, TypeKind::Unspecified, ChannelClose),
      NativeResourceKind::Channel, NativeResourceAccess::InputOutput, 0));
}

} // namespace Simple::VM::Native
