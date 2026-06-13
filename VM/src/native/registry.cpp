#include "native/registry.h"

#include <cstring>
#include <utility>

#include "native/buffer.h"
#include "native/channel.h"
#include "native/json.h"
#include "native/log.h"
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

uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value & 0xFFFFFFFFu);
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

NativeCallResult ChannelClose(NativeCallContext& context) {
  Channel::CloseAll(UnpackI64(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult JsonFree(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(Json::Free(UnpackI64(context.args[0])) ? 1 : 0);
  return result;
}

NativeCallResult LogSetLevel(NativeCallContext& context) {
  Log::SetLevel(UnpackI32(context.args[0]));
  NativeCallResult result;
  result.has_value = false;
  return result;
}

HeapObject* GetHeapObject(NativeCallContext& context, size_t index) {
  if (!context.heap || index >= context.args.size()) return nullptr;
  return context.heap->Get(UnpackRef(context.args[index]));
}

void WriteU32(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  if (offset + 4 > payload.size()) return;
  payload[offset] = static_cast<uint8_t>(value & 0xffu);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

NativeCallResult BufferNew(NativeCallContext& context) {
  NativeCallResult result;
  if (!context.heap) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  const int32_t count = UnpackI32(context.args[0]);
  if (count < 0) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  const uint32_t length = static_cast<uint32_t>(count);
  const uint32_t size = 8u + length * 4u;
  const uint32_t handle = context.heap->Allocate(ObjectKind::List, 0, size);
  HeapObject* obj = context.heap->Get(handle);
  if (!obj) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  WriteU32(obj->payload, 0, length);
  WriteU32(obj->payload, 4, length);
  result.value = PackRef(handle);
  return result;
}

NativeCallResult BufferLen(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  result.value = PackI32(static_cast<int32_t>(Buffer::Len(obj)));
  return result;
}

NativeCallResult BufferReadU16LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  result.value = PackI32(offset < 0 ? 0 : static_cast<int32_t>(
      Buffer::ReadLE(obj, static_cast<uint32_t>(offset), 2u)));
  return result;
}

NativeCallResult BufferReadU32LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  result.value = PackI32(offset < 0 ? 0 : static_cast<int32_t>(
      Buffer::ReadLE(obj, static_cast<uint32_t>(offset), 4u)));
  return result;
}

NativeCallResult BufferWriteU16LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  const uint32_t value = static_cast<uint32_t>(UnpackI32(context.args[2]));
  result.value = PackI32(offset >= 0 && Buffer::WriteLE(obj, static_cast<uint32_t>(offset), 2u,
                                                        value)
                             ? 1
                             : 0);
  return result;
}

NativeCallResult BufferWriteU32LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  const uint32_t value = static_cast<uint32_t>(UnpackI32(context.args[2]));
  result.value = PackI32(offset >= 0 && Buffer::WriteLE(obj, static_cast<uint32_t>(offset), 4u,
                                                        value)
                             ? 1
                             : 0);
  return result;
}

Slot CreateByteList(Heap& heap, const std::vector<uint32_t>& values) {
  const uint32_t length = static_cast<uint32_t>(values.size());
  const uint32_t size = 8u + length * 4u;
  const uint32_t handle = heap.Allocate(ObjectKind::List, 0, size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return PackRef(HeapLayout::kNullRef);
  WriteU32(obj->payload, 0, length);
  WriteU32(obj->payload, 4, length);
  for (uint32_t i = 0; i < length; ++i) {
    WriteU32(obj->payload, 8u + i * 4u, values[i] & 0xffu);
  }
  return PackRef(handle);
}

NativeCallResult BufferSlice(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  const int32_t count = UnpackI32(context.args[2]);
  if (!context.heap || !obj || offset < 0 || count < 0 ||
      static_cast<uint32_t>(offset) > Buffer::Len(obj)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.value = CreateByteList(*context.heap, Buffer::Slice(obj, static_cast<uint32_t>(offset),
                                                             static_cast<uint32_t>(count)));
  return result;
}

NativeCallResult BufferCopy(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* dst = GetHeapObject(context, 0);
  const int32_t dst_offset = UnpackI32(context.args[1]);
  HeapObject* src = GetHeapObject(context, 2);
  const int32_t src_offset = UnpackI32(context.args[3]);
  const int32_t count = UnpackI32(context.args[4]);
  if (dst_offset < 0 || src_offset < 0 || count < 0) {
    result.value = PackI32(0);
    return result;
  }
  result.value = PackI32(static_cast<int32_t>(Buffer::Copy(
      dst, static_cast<uint32_t>(dst_offset), src, static_cast<uint32_t>(src_offset),
      static_cast<uint32_t>(count))));
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

void RegisterSystemJson(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.json", "free", {TypeKind::I64}, TypeKind::I32, JsonFree));
}

void RegisterSystemLog(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.log", "setLevel", {TypeKind::I32}, TypeKind::Unspecified,
                             LogSetLevel));
}

void RegisterSystemBuffer(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.buffer", "new", {TypeKind::I32}, TypeKind::Ref, BufferNew));
  registry.Register(MakeSpec("System.buffer", "len", {TypeKind::Ref}, TypeKind::I32, BufferLen));
  registry.Register(MakeSpec("System.buffer", "readU16LE", {TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, BufferReadU16LE));
  registry.Register(MakeSpec("System.buffer", "readU32LE", {TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, BufferReadU32LE));
  registry.Register(MakeSpec("System.buffer", "writeU16LE",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             BufferWriteU16LE));
  registry.Register(MakeSpec("System.buffer", "writeU32LE",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             BufferWriteU32LE));
  registry.Register(MakeSpec("System.buffer", "slice",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::Ref,
                             BufferSlice));
  registry.Register(MakeSpec("System.buffer", "copy",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::Ref, TypeKind::I32,
                              TypeKind::I32},
                             TypeKind::I32, BufferCopy));
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
  registry.Register(MakeSpec("System.channel", "newF32", {}, TypeKind::I64, ChannelNewF32));
  registry.Register(MakeSpec("System.channel", "sendF32", {TypeKind::I64, TypeKind::F32},
                             TypeKind::I32, ChannelSendF32));
  registry.Register(MakeSpec("System.channel", "trySendF32", {TypeKind::I64, TypeKind::F32},
                             TypeKind::I32, ChannelSendF32));
  registry.Register(MakeSpec("System.channel", "recvF32", {TypeKind::I64}, TypeKind::F32,
                             ChannelRecvF32));
  registry.Register(MakeSpec("System.channel", "tryRecvF32", {TypeKind::I64}, TypeKind::F32,
                             ChannelTryRecvF32));
  registry.Register(MakeSpec("System.channel", "pendingF32", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingF32));
  registry.Register(MakeSpec("System.channel", "newF64", {}, TypeKind::I64, ChannelNewF64));
  registry.Register(MakeSpec("System.channel", "sendF64", {TypeKind::I64, TypeKind::F64},
                             TypeKind::I32, ChannelSendF64));
  registry.Register(MakeSpec("System.channel", "trySendF64", {TypeKind::I64, TypeKind::F64},
                             TypeKind::I32, ChannelSendF64));
  registry.Register(MakeSpec("System.channel", "recvF64", {TypeKind::I64}, TypeKind::F64,
                             ChannelRecvF64));
  registry.Register(MakeSpec("System.channel", "tryRecvF64", {TypeKind::I64}, TypeKind::F64,
                             ChannelTryRecvF64));
  registry.Register(MakeSpec("System.channel", "pendingF64", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingF64));
  registry.Register(MakeSpec("System.channel", "newBool", {}, TypeKind::I64, ChannelNewBool));
  registry.Register(MakeSpec("System.channel", "sendBool", {TypeKind::I64, TypeKind::Bool},
                             TypeKind::I32, ChannelSendBool));
  registry.Register(MakeSpec("System.channel", "trySendBool", {TypeKind::I64, TypeKind::Bool},
                             TypeKind::I32, ChannelSendBool));
  registry.Register(MakeSpec("System.channel", "recvBool", {TypeKind::I64}, TypeKind::Bool,
                             ChannelRecvBool));
  registry.Register(MakeSpec("System.channel", "tryRecvBool", {TypeKind::I64}, TypeKind::Bool,
                             ChannelTryRecvBool));
  registry.Register(MakeSpec("System.channel", "pendingBool", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingBool));
  registry.Register(MakeSpec("System.channel", "newString", {}, TypeKind::I64, ChannelNewString));
  registry.Register(MakeSpec("System.channel", "pendingString", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingString));
  registry.Register(MakeSpec("System.channel", "newBytes", {}, TypeKind::I64, ChannelNewBytes));
  registry.Register(MakeSpec("System.channel", "pendingBytes", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingBytes));
  registry.Register(MakeSpec("System.channel", "close", {TypeKind::I64}, TypeKind::Unspecified,
                             ChannelClose));
}

NativeRegistry BuildDefaultRegistry() {
  NativeRegistry registry;
  RegisterSystemRandom(registry);
  RegisterSystemOs(registry);
  RegisterSystemThread(registry);
  RegisterSystemChannel(registry);
  RegisterSystemJson(registry);
  RegisterSystemLog(registry);
  RegisterSystemBuffer(registry);
  return registry;
}

} // namespace Simple::VM::Native
