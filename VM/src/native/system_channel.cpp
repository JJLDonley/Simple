#include "native/registry.h"

#include "native/arg_utils.h"
#include "native/channel.h"
#include "native/spec_builder.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

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

Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

uint32_t UnpackU32Bits(Slot value) {
  return static_cast<uint32_t>(value);
}

uint64_t UnpackU64Bits(Slot value) {
  return static_cast<uint64_t>(value);
}

float BitsToF32(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double BitsToF64(uint64_t bits) {
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

Slot PackF32(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

Slot PackF64(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

NativeCallResult ChannelNewI32(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_i32));
  return result;
}

NativeCallResult ChannelSendI32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_i32, UnpackI64(context.args[0]),
                                       UnpackI32(context.args[1]))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvI32(NativeCallContext& context) {
  int32_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i32, UnpackI64(context.args[0]), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value);
  return result;
}

NativeCallResult ChannelTryRecvI32(NativeCallContext& context) {
  int32_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i32, UnpackI64(context.args[0]), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value);
  return result;
}

NativeCallResult ChannelPendingI32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_i32, UnpackI64(context.args[0])));
  return result;
}

NativeCallResult ChannelNewI64(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_i64));
  return result;
}

NativeCallResult ChannelSendI64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_i64, UnpackI64(context.args[0]),
                                       UnpackI64(context.args[1]))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvI64(NativeCallContext& context) {
  int64_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i64, UnpackI64(context.args[0]), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI64(value);
  return result;
}

NativeCallResult ChannelTryRecvI64(NativeCallContext& context) {
  int64_t value = 0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_i64, UnpackI64(context.args[0]), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI64(value);
  return result;
}

NativeCallResult ChannelPendingI64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_i64, UnpackI64(context.args[0])));
  return result;
}

NativeCallResult ChannelNewBool(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_bool));
  return result;
}

NativeCallResult ChannelSendBool(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_bool, UnpackI64(context.args[0]),
                                       UnpackI32(context.args[1]) != 0)
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvBool(NativeCallContext& context) {
  bool value = false;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_bool, UnpackI64(context.args[0]), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value ? 1 : 0);
  return result;
}

NativeCallResult ChannelTryRecvBool(NativeCallContext& context) {
  bool value = false;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_bool, UnpackI64(context.args[0]), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(value ? 1 : 0);
  return result;
}

NativeCallResult ChannelPendingBool(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_bool, UnpackI64(context.args[0])));
  return result;
}

NativeCallResult ChannelNewF32(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_f32));
  return result;
}

NativeCallResult ChannelSendF32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_f32, UnpackI64(context.args[0]),
                                       BitsToF32(UnpackU32Bits(context.args[1])))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvF32(NativeCallContext& context) {
  float value = 0.0f;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f32, UnpackI64(context.args[0]), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF32(value);
  return result;
}

NativeCallResult ChannelTryRecvF32(NativeCallContext& context) {
  float value = 0.0f;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f32, UnpackI64(context.args[0]), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF32(value);
  return result;
}

NativeCallResult ChannelPendingF32(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_f32, UnpackI64(context.args[0])));
  return result;
}

NativeCallResult ChannelNewF64(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_f64));
  return result;
}

NativeCallResult ChannelSendF64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Send(Channel::g_f64, UnpackI64(context.args[0]),
                                       BitsToF64(UnpackU64Bits(context.args[1])))
                              ? 1
                              : 0);
  return result;
}

NativeCallResult ChannelRecvF64(NativeCallContext& context) {
  double value = 0.0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f64, UnpackI64(context.args[0]), true, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF64(value);
  return result;
}

NativeCallResult ChannelTryRecvF64(NativeCallContext& context) {
  double value = 0.0;
  NativeCallResult result;
  if (!Channel::Receive(Channel::g_f64, UnpackI64(context.args[0]), false, &value)) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackF64(value);
  return result;
}

NativeCallResult ChannelPendingF64(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_f64, UnpackI64(context.args[0])));
  return result;
}

NativeCallResult ChannelNewString(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_string));
  return result;
}

NativeCallResult ChannelPendingString(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_string, UnpackI64(context.args[0])));
  return result;
}

NativeCallResult ChannelNewBytes(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Channel::New(Channel::g_bytes));
  return result;
}

NativeCallResult ChannelPendingBytes(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Channel::Pending(Channel::g_bytes, UnpackI64(context.args[0])));
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
                                 Channel::Send(Channel::g_string, UnpackI64(context.args[0]),
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
                                 Channel::Send(Channel::g_bytes, UnpackI64(context.args[0]), values)
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
  if (!Channel::Receive(Channel::g_string, UnpackI64(context.args[0]), wait, &value)) {
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
  if (!context.heap || !Channel::Receive(Channel::g_bytes, UnpackI64(context.args[0]), wait, &values)) {
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
  Channel::CloseAll(UnpackI64(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
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
    registry.Register(MakeSpec(module, (std::string("new") + suffix_text).c_str(), {}, TypeKind::I64,
                               std::move(new_handler)));
    registry.Register(MakeSpec(module, ("send" + suffix_text).c_str(), {TypeKind::I64, value_type},
                               TypeKind::I32, std::move(send_handler)));
    registry.Register(MakeSpec(module, ("trySend" + suffix_text).c_str(), {TypeKind::I64, value_type},
                               TypeKind::I32, std::move(try_send_handler)));
    registry.Register(MakeSpec(module, ("recv" + suffix_text).c_str(), {TypeKind::I64}, value_type,
                               std::move(recv_handler)));
    registry.Register(MakeSpec(module, ("tryRecv" + suffix_text).c_str(), {TypeKind::I64}, value_type,
                               std::move(try_recv_handler)));
    registry.Register(MakeSpec(module, ("pending" + suffix_text).c_str(), {TypeKind::I64}, TypeKind::I32,
                               std::move(pending_handler)));
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

  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemChannelMember::Close), {TypeKind::I64}, TypeKind::Unspecified,
                             ChannelClose));
}

} // namespace Simple::VM::Native
