#include "native/registry.h"

#include <cstring>
#include <utility>

#include "native/channel.h"
#include "native/random.h"
#include "native/thread.h"
#include "native/time.h"

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

Slot PackF64(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

NativeCallResult RandomSeed(NativeCallContext& context) {
  Random::Seed(static_cast<uint64_t>(UnpackI64(context.args[0])));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult RandomI32(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI32(Random::I32());
  return result;
}

NativeCallResult RandomRange(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Random::Range(UnpackI32(context.args[0]), UnpackI32(context.args[1])));
  return result;
}

NativeCallResult RandomF64(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackF64(Random::F64());
  return result;
}

NativeCallResult OsTimeMonoNs(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Time::MonotonicNs());
  return result;
}

NativeCallResult OsTimeWallNs(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI64(Time::WallNs());
  return result;
}

NativeCallResult OsSleepMs(NativeCallContext& context) {
  Thread::SleepMs(UnpackI32(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult ThreadSleep(NativeCallContext& context) {
  Thread::SleepMs(UnpackI32(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult ThreadYield(NativeCallContext&) {
  Thread::Yield();
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult ThreadHardwareConcurrency(NativeCallContext&) {
  NativeCallResult result;
  result.value = PackI32(Thread::HardwareConcurrency());
  return result;
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

NativeCallResult ChannelClose(NativeCallContext& context) {
  Channel::CloseAll(UnpackI64(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeFunctionSpec MakeSpec(const char* module_name, const char* symbol_name,
                            std::vector<Simple::Byte::TypeKind> params,
                            Simple::Byte::TypeKind result_type,
                            NativeFunctionHandler handler) {
  NativeFunctionSpec spec;
  spec.module_name = module_name;
  spec.symbol_name = symbol_name;
  spec.parameter_types = std::move(params);
  spec.result_type = result_type;
  spec.handler = std::move(handler);
  return spec;
}

} // namespace

bool NativeRegistry::Register(NativeFunctionSpec spec) {
  if (spec.module_name.empty() || spec.symbol_name.empty() || !spec.handler) return false;
  if (Find(spec.module_name, spec.symbol_name)) return false;
  functions_.push_back(std::move(spec));
  return true;
}

const NativeFunctionSpec* NativeRegistry::Find(const std::string& module_name,
                                               const std::string& symbol_name) const {
  for (const NativeFunctionSpec& spec : functions_) {
    if (spec.module_name == module_name && spec.symbol_name == symbol_name) return &spec;
  }
  return nullptr;
}

size_t NativeRegistry::Size() const {
  return functions_.size();
}

void RegisterSystemRandom(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.random", "seed", {TypeKind::I64}, TypeKind::Unspecified,
                             RandomSeed));
  registry.Register(MakeSpec("System.random", "i32", {}, TypeKind::I32, RandomI32));
  registry.Register(MakeSpec("System.random", "range", {TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             RandomRange));
  registry.Register(MakeSpec("System.random", "f64", {}, TypeKind::F64, RandomF64));
}

void RegisterSystemOs(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.os", "time_mono_ns", {}, TypeKind::I64, OsTimeMonoNs));
  registry.Register(MakeSpec("System.os", "time_wall_ns", {}, TypeKind::I64, OsTimeWallNs));
  registry.Register(MakeSpec("System.os", "sleep_ms", {TypeKind::I32}, TypeKind::Unspecified,
                             OsSleepMs));
}

void RegisterSystemThread(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.thread", "sleep", {TypeKind::I32}, TypeKind::Unspecified,
                             ThreadSleep));
  registry.Register(MakeSpec("System.thread", "yield", {}, TypeKind::Unspecified, ThreadYield));
  registry.Register(MakeSpec("System.thread", "hardwareConcurrency", {}, TypeKind::I32,
                             ThreadHardwareConcurrency));
}

void RegisterSystemChannel(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.channel", "newI32", {}, TypeKind::I64, ChannelNewI32));
  registry.Register(MakeSpec("System.channel", "sendI32", {TypeKind::I64, TypeKind::I32},
                             TypeKind::I32, ChannelSendI32));
  registry.Register(MakeSpec("System.channel", "trySendI32", {TypeKind::I64, TypeKind::I32},
                             TypeKind::I32, ChannelSendI32));
  registry.Register(MakeSpec("System.channel", "recvI32", {TypeKind::I64}, TypeKind::I32,
                             ChannelRecvI32));
  registry.Register(MakeSpec("System.channel", "tryRecvI32", {TypeKind::I64}, TypeKind::I32,
                             ChannelTryRecvI32));
  registry.Register(MakeSpec("System.channel", "pendingI32", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingI32));
  registry.Register(MakeSpec("System.channel", "newI64", {}, TypeKind::I64, ChannelNewI64));
  registry.Register(MakeSpec("System.channel", "sendI64", {TypeKind::I64, TypeKind::I64},
                             TypeKind::I32, ChannelSendI64));
  registry.Register(MakeSpec("System.channel", "trySendI64", {TypeKind::I64, TypeKind::I64},
                             TypeKind::I32, ChannelSendI64));
  registry.Register(MakeSpec("System.channel", "recvI64", {TypeKind::I64}, TypeKind::I64,
                             ChannelRecvI64));
  registry.Register(MakeSpec("System.channel", "tryRecvI64", {TypeKind::I64}, TypeKind::I64,
                             ChannelTryRecvI64));
  registry.Register(MakeSpec("System.channel", "pendingI64", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingI64));
  registry.Register(MakeSpec("System.channel", "close", {TypeKind::I64}, TypeKind::Unspecified,
                             ChannelClose));
}

NativeRegistry BuildDefaultRegistry() {
  NativeRegistry registry;
  RegisterSystemRandom(registry);
  RegisterSystemOs(registry);
  RegisterSystemThread(registry);
  RegisterSystemChannel(registry);
  return registry;
}

} // namespace Simple::VM::Native
