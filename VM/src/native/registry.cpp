#include "native/registry.h"

#include <cstring>
#include <utility>

#include "native/buffer.h"
#include "native/channel.h"
#include "native/env.h"
#include "native/fs.h"
#include "native/json.h"
#include "native/log.h"
#include "native/os.h"
#include "native/path.h"
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

bool ReadStringArg(NativeCallContext& context, size_t index, std::string* out_value);
void WriteU32(std::vector<uint8_t>& payload, size_t offset, uint32_t value);

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

NativeCallResult OsCwdGet(NativeCallContext&) {
  NativeCallResult result;
  if (!Os::CurrentWorkingDirectory(&result.string_value)) {
    result.value = PackRef(HeapLayout::kNullRef);
  }
  return result;
}

NativeCallResult OsFormatWallNs(NativeCallContext& context) {
  NativeCallResult result;
  result.string_value = Time::FormatWallNsUtc(UnpackI64(context.args[0]));
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

NativeCallResult LogSetFile(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  result.value = PackI32(ReadStringArg(context, 0, &path) && Log::SetFile(path) ? 1 : 0);
  return result;
}

NativeCallResult LogEmit(NativeCallContext& context) {
  NativeCallResult result;
  std::string message;
  if (ReadStringArg(context, 0, &message)) Log::Emit(message, UnpackI32(context.args[1]));
  result.has_value = false;
  return result;
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

NativeCallResult EnvArgsCount(NativeCallContext& context) {
  NativeCallResult result;
  result.value = PackI32(context.argv ? static_cast<int32_t>(context.argv->size()) : 0);
  return result;
}

NativeCallResult EnvArg(NativeCallContext& context) {
  NativeCallResult result;
  const int32_t index = UnpackI32(context.args[0]);
  if (!context.argv || index < 0 || static_cast<size_t>(index) >= context.argv->size()) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = (*context.argv)[static_cast<size_t>(index)];
  return result;
}

NativeCallResult EnvGet(NativeCallContext& context) {
  NativeCallResult result;
  std::string name;
  std::string storage;
  const char* value = ReadStringArg(context, 0, &name) ? Env::Get(name, &storage) : nullptr;
  if (!value) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = value;
  return result;
}

NativeCallResult EnvSet(NativeCallContext& context) {
  NativeCallResult result;
  std::string name;
  std::string value;
  result.value = PackI32(ReadStringArg(context, 0, &name) && ReadStringArg(context, 1, &value) &&
                                 Env::Set(name, value)
                             ? 1
                             : 0);
  return result;
}

NativeCallResult EnvPlatform(NativeCallContext&) {
  NativeCallResult result;
  result.string_value = Env::PlatformName();
  return result;
}

NativeCallResult EnvArch(NativeCallContext&) {
  NativeCallResult result;
  result.string_value = Env::ArchName();
  return result;
}

NativeCallResult EnvExePath(NativeCallContext&) {
  NativeCallResult result;
  result.string_value = Env::ExePath();
  return result;
}

HeapObject* GetHeapObject(NativeCallContext& context, size_t index) {
  if (!context.heap || index >= context.args.size()) return nullptr;
  return context.heap->Get(UnpackRef(context.args[index]));
}

std::string ReadStringAscii(const HeapObject* obj) {
  if (!obj || obj->header.kind != ObjectKind::String || obj->payload.size() < 4) return {};
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  if (4u + length * 2u > obj->payload.size()) return {};
  std::string out;
  out.reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    const size_t offset = 4u + i * 2u;
    const uint16_t ch = obj->payload[offset] | (static_cast<uint16_t>(obj->payload[offset + 1]) << 8u);
    out.push_back(ch <= 0x7fu ? static_cast<char>(ch) : '?');
  }
  return out;
}

bool ReadStringArg(NativeCallContext& context, size_t index, std::string* out_value) {
  if (!out_value) return false;
  HeapObject* obj = GetHeapObject(context, index);
  if (!obj || obj->header.kind != ObjectKind::String) return false;
  *out_value = ReadStringAscii(obj);
  return true;
}

NativeCallResult PathJoin(NativeCallContext& context) {
  NativeCallResult result;
  std::string left;
  std::string right;
  if (!ReadStringArg(context, 0, &left) || !ReadStringArg(context, 1, &right)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = Path::Join(left, right);
  return result;
}

NativeCallResult PathDirname(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  if (!ReadStringArg(context, 0, &value)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = Path::Dirname(value);
  return result;
}

NativeCallResult PathBasename(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  if (!ReadStringArg(context, 0, &value)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = Path::Basename(value);
  return result;
}

NativeCallResult PathExt(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  if (!ReadStringArg(context, 0, &value)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = Path::Extension(value);
  return result;
}

NativeCallResult PathNormalize(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  if (!ReadStringArg(context, 0, &value)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = Path::Normalize(value);
  return result;
}

NativeCallResult PathExists(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  result.value = PackI32(ReadStringArg(context, 0, &value) && Path::Exists(value) ? 1 : 0);
  return result;
}

NativeCallResult PathIsFile(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  result.value = PackI32(ReadStringArg(context, 0, &value) && Path::IsFile(value) ? 1 : 0);
  return result;
}

NativeCallResult PathIsDir(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  result.value = PackI32(ReadStringArg(context, 0, &value) && Path::IsDir(value) ? 1 : 0);
  return result;
}

NativeCallResult FsReadText(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  if (!ReadStringArg(context, 0, &path) || !Fs::ReadText(path, &result.string_value)) {
    result.value = PackRef(HeapLayout::kNullRef);
  }
  return result;
}

NativeCallResult FsWriteText(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  std::string text;
  result.value = PackI32(ReadStringArg(context, 0, &path) && ReadStringArg(context, 1, &text) &&
                                 Fs::WriteText(path, text)
                             ? 1
                             : 0);
  return result;
}

NativeCallResult FsCwd(NativeCallContext&) {
  NativeCallResult result;
  if (!Fs::Cwd(&result.string_value)) {
    result.value = PackRef(HeapLayout::kNullRef);
  }
  return result;
}

NativeCallResult FsCopy(NativeCallContext& context) {
  NativeCallResult result;
  std::string from;
  std::string to;
  result.value = PackI32(ReadStringArg(context, 0, &from) && ReadStringArg(context, 1, &to) &&
                                 Fs::CopyFile(from, to)
                             ? 1
                             : 0);
  return result;
}

NativeCallResult FsRemove(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  result.value = PackI32(ReadStringArg(context, 0, &path) && Fs::Remove(path) ? 1 : 0);
  return result;
}

NativeCallResult FsMkdir(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  result.value = PackI32(ReadStringArg(context, 0, &path) && Fs::Mkdir(path) ? 1 : 0);
  return result;
}

NativeCallResult FsMkdirAll(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  result.value = PackI32(ReadStringArg(context, 0, &path) && Fs::MkdirAll(path) ? 1 : 0);
  return result;
}

NativeCallResult FsOpen(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  if (!context.open_files || !ReadStringArg(context, 0, &path)) {
    result.value = PackI32(-1);
    return result;
  }
  const int32_t flags = UnpackI32(context.args[1]);
  const char* mode = "rb";
  if (flags & 0x2) {
    mode = "ab";
  } else if (flags & 0x1) {
    mode = "wb";
  }
  std::FILE* file = Fs::OpenFile(path, mode);
  if (!file) {
    result.value = PackI32(-1);
    return result;
  }
  context.open_files->push_back(file);
  result.value = PackI32(static_cast<int32_t>(context.open_files->size() - 1));
  return result;
}

NativeCallResult FsRead(NativeCallContext& context) {
  NativeCallResult result;
  const int32_t fd = UnpackI32(context.args[0]);
  const int32_t count = UnpackI32(context.args[2]);
  HeapObject* obj = GetHeapObject(context, 1);
  if (!context.open_files || fd < 0 || static_cast<size_t>(fd) >= context.open_files->size() ||
      !(*context.open_files)[static_cast<size_t>(fd)] || count < 0 || !obj ||
      obj->header.kind != ObjectKind::Array || obj->payload.size() < 4) {
    result.value = PackI32(-1);
    return result;
  }
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  uint32_t req = static_cast<uint32_t>(count);
  if (req > length) req = length;
  if (4u + static_cast<size_t>(length) * 4u > obj->payload.size()) {
    result.value = PackI32(-1);
    return result;
  }
  std::vector<uint8_t> bytes(req);
  const size_t got = req > 0 ? std::fread(bytes.data(), 1, req, (*context.open_files)[static_cast<size_t>(fd)]) : 0;
  for (size_t i = 0; i < got; ++i) WriteU32(obj->payload, 4u + i * 4u, bytes[i]);
  result.value = PackI32(static_cast<int32_t>(got));
  return result;
}

NativeCallResult FsWrite(NativeCallContext& context) {
  NativeCallResult result;
  const int32_t fd = UnpackI32(context.args[0]);
  const int32_t count = UnpackI32(context.args[2]);
  HeapObject* obj = GetHeapObject(context, 1);
  if (!context.open_files || fd < 0 || static_cast<size_t>(fd) >= context.open_files->size() ||
      !(*context.open_files)[static_cast<size_t>(fd)] || count < 0 || !obj ||
      obj->header.kind != ObjectKind::Array || obj->payload.size() < 4) {
    result.value = PackI32(-1);
    return result;
  }
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  uint32_t req = static_cast<uint32_t>(count);
  if (req > length) req = length;
  if (4u + static_cast<size_t>(length) * 4u > obj->payload.size()) {
    result.value = PackI32(-1);
    return result;
  }
  std::vector<uint8_t> bytes(req);
  for (uint32_t i = 0; i < req; ++i) {
    bytes[i] = static_cast<uint8_t>(obj->payload[4u + i * 4u]);
  }
  const size_t wrote = req > 0 ? std::fwrite(bytes.data(), 1, req, (*context.open_files)[static_cast<size_t>(fd)]) : 0;
  result.value = PackI32(static_cast<int32_t>(wrote));
  return result;
}

NativeCallResult FsClose(NativeCallContext& context) {
  NativeCallResult result;
  result.has_value = false;
  if (!context.open_files) return result;
  const int32_t fd = UnpackI32(context.args[0]);
  if (fd < 0 || static_cast<size_t>(fd) >= context.open_files->size()) return result;
  std::FILE* file = (*context.open_files)[static_cast<size_t>(fd)];
  if (file) {
    std::fclose(file);
    (*context.open_files)[static_cast<size_t>(fd)] = nullptr;
  }
  return result;
}

NativeCallResult FsSetCwd(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  result.value = PackI32(ReadStringArg(context, 0, &path) && Fs::SetCwd(path) ? 1 : 0);
  return result;
}

void WriteU32(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  if (offset + 4 > payload.size()) return;
  payload[offset] = static_cast<uint8_t>(value & 0xffu);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16u) & 0xffu);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24u) & 0xffu);
}

uint32_t ReadU32(const std::vector<uint8_t>& payload, size_t offset) {
  if (offset + 4 > payload.size()) return 0;
  return payload[offset] | (static_cast<uint32_t>(payload[offset + 1]) << 8u) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16u) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24u);
}

bool IsBufferObject(const HeapObject* obj) {
  return obj && (obj->header.kind == ObjectKind::List || obj->header.kind == ObjectKind::Array) &&
         obj->payload.size() >= 4;
}

size_t BufferElementBase(const HeapObject* obj) {
  return obj && obj->header.kind == ObjectKind::List ? 8u : 4u;
}

Slot CreateStringAscii(Heap& heap, const std::string& value) {
  const uint32_t length = static_cast<uint32_t>(value.size());
  const uint32_t handle = heap.Allocate(ObjectKind::String, 0, 4u + length * 2u);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return PackRef(HeapLayout::kNullRef);
  WriteU32(obj->payload, 0, length);
  for (uint32_t i = 0; i < length; ++i) {
    const size_t offset = 4u + i * 2u;
    obj->payload[offset] = static_cast<uint8_t>(value[i]);
    obj->payload[offset + 1] = 0;
  }
  return PackRef(handle);
}

Slot CreateRefList(Heap& heap, const std::vector<uint32_t>& refs) {
  const uint32_t length = static_cast<uint32_t>(refs.size());
  const uint32_t handle = heap.Allocate(ObjectKind::List, 0, 8u + length * 4u);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return PackRef(HeapLayout::kNullRef);
  WriteU32(obj->payload, 0, length);
  WriteU32(obj->payload, 4, length);
  for (uint32_t i = 0; i < length; ++i) WriteU32(obj->payload, 8u + i * 4u, refs[i]);
  return PackRef(handle);
}

bool ReadByteList(NativeCallContext& context, size_t index, std::vector<int32_t>* out) {
  if (!out) return false;
  HeapObject* obj = GetHeapObject(context, index);
  if (!obj || (obj->header.kind != ObjectKind::List && obj->header.kind != ObjectKind::Array) ||
      obj->payload.size() < 4) {
    return false;
  }
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  const size_t elem_base = obj->header.kind == ObjectKind::List ? 8u : 4u;
  if (elem_base + static_cast<size_t>(length) * 4u > obj->payload.size()) return false;
  out->clear();
  out->reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    const size_t offset = elem_base + i * 4u;
    const uint32_t value = obj->payload[offset] |
                           (static_cast<uint32_t>(obj->payload[offset + 1]) << 8u) |
                           (static_cast<uint32_t>(obj->payload[offset + 2]) << 16u) |
                           (static_cast<uint32_t>(obj->payload[offset + 3]) << 24u);
    out->push_back(static_cast<int32_t>(value));
  }
  return true;
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

NativeCallResult FsReadBytes(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  std::vector<int32_t> values;
  if (!context.heap || !ReadStringArg(context, 0, &path) || !Fs::ReadBytes(path, &values)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  std::vector<uint32_t> bytes;
  bytes.reserve(values.size());
  for (int32_t value : values) bytes.push_back(static_cast<uint32_t>(value));
  result.value = CreateByteList(*context.heap, bytes);
  return result;
}

NativeCallResult FsWriteBytes(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  std::vector<int32_t> values;
  result.value = PackI32(ReadStringArg(context, 0, &path) && ReadByteList(context, 1, &values) &&
                                 Fs::WriteBytes(path, values)
                             ? 1
                             : 0);
  return result;
}

NativeCallResult FsListDir(NativeCallContext& context) {
  NativeCallResult result;
  std::string path;
  std::vector<std::string> names;
  if (!context.heap || !ReadStringArg(context, 0, &path) || !Fs::ListDir(path, &names)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  std::vector<uint32_t> refs;
  refs.reserve(names.size());
  for (const std::string& name : names) {
    refs.push_back(static_cast<uint32_t>(CreateStringAscii(*context.heap, name)));
  }
  result.value = CreateRefList(*context.heap, refs);
  return result;
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

NativeCallResult IoBufferNew(NativeCallContext& context) {
  NativeCallResult result;
  if (!context.heap) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  const int32_t requested = UnpackI32(context.args[0]);
  if (requested < 0) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  const uint32_t length = static_cast<uint32_t>(requested);
  const uint32_t handle = context.heap->Allocate(ObjectKind::List, 0, 8u + length * 4u);
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

NativeCallResult IoBufferLen(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  result.value = PackI32(IsBufferObject(obj) ? static_cast<int32_t>(ReadU32(obj->payload, 0)) : -1);
  return result;
}

NativeCallResult IoBufferFill(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = GetHeapObject(context, 0);
  const int32_t value = UnpackI32(context.args[1]);
  const int32_t count = UnpackI32(context.args[2]);
  if (!IsBufferObject(obj) || count < 0) {
    result.value = PackI32(-1);
    return result;
  }
  uint32_t n = static_cast<uint32_t>(count);
  const uint32_t length = ReadU32(obj->payload, 0);
  if (n > length) n = length;
  const size_t base = BufferElementBase(obj);
  if (base + static_cast<size_t>(n) * 4u > obj->payload.size()) {
    result.value = PackI32(-1);
    return result;
  }
  for (uint32_t i = 0; i < n; ++i) WriteU32(obj->payload, base + i * 4u, static_cast<uint32_t>(value));
  result.value = PackI32(static_cast<int32_t>(n));
  return result;
}

NativeCallResult IoBufferCopy(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* dst = GetHeapObject(context, 0);
  HeapObject* src = GetHeapObject(context, 1);
  const int32_t count = UnpackI32(context.args[2]);
  if (!IsBufferObject(dst) || !IsBufferObject(src) || count < 0) {
    result.value = PackI32(-1);
    return result;
  }
  uint32_t n = static_cast<uint32_t>(count);
  const uint32_t dst_len = ReadU32(dst->payload, 0);
  const uint32_t src_len = ReadU32(src->payload, 0);
  if (n > dst_len) n = dst_len;
  if (n > src_len) n = src_len;
  const size_t dst_base = BufferElementBase(dst);
  const size_t src_base = BufferElementBase(src);
  if (dst_base + static_cast<size_t>(n) * 4u > dst->payload.size() ||
      src_base + static_cast<size_t>(n) * 4u > src->payload.size()) {
    result.value = PackI32(-1);
    return result;
  }
  for (uint32_t i = 0; i < n; ++i) {
    WriteU32(dst->payload, dst_base + i * 4u, ReadU32(src->payload, src_base + i * 4u));
  }
  result.value = PackI32(static_cast<int32_t>(n));
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
  registry.Register(MakeSpec("System.os", "args_count", {}, TypeKind::I32, EnvArgsCount));
  registry.Register(MakeSpec("System.os", "args_get", {TypeKind::I32}, TypeKind::String,
                             EnvArg));
  registry.Register(MakeSpec("System.os", "env_get", {TypeKind::String}, TypeKind::String,
                             EnvGet));
  registry.Register(MakeSpec("System.os", "time_mono_ns", {}, TypeKind::I64, OsTimeMonoNs));
  registry.Register(MakeSpec("System.os", "time_wall_ns", {}, TypeKind::I64, OsTimeWallNs));
  registry.Register(MakeSpec("System.os", "sleep_ms", {TypeKind::I32}, TypeKind::Unspecified,
                             OsSleepMs));
  registry.Register(MakeSpec("System.os", "cwd_get", {}, TypeKind::String, OsCwdGet));
  registry.Register(MakeSpec("System.os", "formatWallNs", {TypeKind::I64}, TypeKind::String,
                             OsFormatWallNs));
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
  registry.Register(MakeSpec("System.log", "setFile", {TypeKind::String}, TypeKind::I32,
                             LogSetFile));
  registry.Register(MakeSpec("System.log", "log", {TypeKind::String, TypeKind::I32},
                             TypeKind::Unspecified, LogEmit));
  registry.Register(MakeSpec("System.log", "info", {TypeKind::String}, TypeKind::Unspecified,
                             LogInfo));
  registry.Register(MakeSpec("System.log", "warn", {TypeKind::String}, TypeKind::Unspecified,
                             LogWarn));
  registry.Register(MakeSpec("System.log", "error", {TypeKind::String}, TypeKind::Unspecified,
                             LogError));
}

void RegisterSystemPath(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.path", "join", {TypeKind::String, TypeKind::String},
                             TypeKind::String, PathJoin));
  registry.Register(MakeSpec("System.path", "dirname", {TypeKind::String}, TypeKind::String,
                             PathDirname));
  registry.Register(MakeSpec("System.path", "basename", {TypeKind::String}, TypeKind::String,
                             PathBasename));
  registry.Register(MakeSpec("System.path", "ext", {TypeKind::String}, TypeKind::String,
                             PathExt));
  registry.Register(MakeSpec("System.path", "normalize", {TypeKind::String}, TypeKind::String,
                             PathNormalize));
  registry.Register(MakeSpec("System.path", "exists", {TypeKind::String}, TypeKind::I32,
                             PathExists));
  registry.Register(MakeSpec("System.path", "isFile", {TypeKind::String}, TypeKind::I32,
                             PathIsFile));
  registry.Register(MakeSpec("System.path", "isDir", {TypeKind::String}, TypeKind::I32,
                             PathIsDir));
}

void RegisterSystemFs(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.fs", "readText", {TypeKind::String}, TypeKind::String,
                             FsReadText));
  registry.Register(MakeSpec("System.fs", "writeText", {TypeKind::String, TypeKind::String},
                             TypeKind::I32, FsWriteText));
  registry.Register(MakeSpec("System.fs", "readBytes", {TypeKind::String}, TypeKind::Ref,
                             FsReadBytes));
  registry.Register(MakeSpec("System.fs", "writeBytes", {TypeKind::String, TypeKind::Ref},
                             TypeKind::I32, FsWriteBytes));
  registry.Register(MakeSpec("System.fs", "listDir", {TypeKind::String}, TypeKind::Ref,
                             FsListDir));
  registry.Register(MakeSpec("System.fs", "open", {TypeKind::String, TypeKind::I32},
                             TypeKind::I32, FsOpen));
  registry.Register(MakeSpec("System.fs", "read", {TypeKind::I32, TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, FsRead));
  registry.Register(MakeSpec("System.fs", "write", {TypeKind::I32, TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, FsWrite));
  registry.Register(MakeSpec("System.fs", "close", {TypeKind::I32}, TypeKind::Unspecified,
                             FsClose));
  registry.Register(MakeSpec("System.fs", "cwd", {}, TypeKind::String, FsCwd));
  registry.Register(MakeSpec("System.fs", "copy", {TypeKind::String, TypeKind::String},
                             TypeKind::I32, FsCopy));
  registry.Register(MakeSpec("System.fs", "remove", {TypeKind::String}, TypeKind::I32,
                             FsRemove));
  registry.Register(MakeSpec("System.fs", "mkdir", {TypeKind::String}, TypeKind::I32,
                             FsMkdir));
  registry.Register(MakeSpec("System.fs", "mkdirAll", {TypeKind::String}, TypeKind::I32,
                             FsMkdirAll));
  registry.Register(MakeSpec("System.fs", "setCwd", {TypeKind::String}, TypeKind::I32,
                             FsSetCwd));
}

void RegisterSystemEnv(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.env", "argsCount", {}, TypeKind::I32, EnvArgsCount));
  registry.Register(MakeSpec("System.env", "arg", {TypeKind::I32}, TypeKind::String, EnvArg));
  registry.Register(MakeSpec("System.env", "get", {TypeKind::String}, TypeKind::String,
                             EnvGet));
  registry.Register(MakeSpec("System.env", "set", {TypeKind::String, TypeKind::String},
                             TypeKind::I32, EnvSet));
  registry.Register(MakeSpec("System.env", "platform", {}, TypeKind::String, EnvPlatform));
  registry.Register(MakeSpec("System.env", "arch", {}, TypeKind::String, EnvArch));
  registry.Register(MakeSpec("System.env", "exePath", {}, TypeKind::String, EnvExePath));
}

void RegisterSystemIo(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.io", "buffer_new", {TypeKind::I32}, TypeKind::Ref,
                             IoBufferNew));
  registry.Register(MakeSpec("System.io", "buffer_len", {TypeKind::Ref}, TypeKind::I32,
                             IoBufferLen));
  registry.Register(MakeSpec("System.io", "buffer_fill", {TypeKind::Ref, TypeKind::I32,
                                                            TypeKind::I32},
                             TypeKind::I32, IoBufferFill));
  registry.Register(MakeSpec("System.io", "buffer_copy", {TypeKind::Ref, TypeKind::Ref,
                                                            TypeKind::I32},
                             TypeKind::I32, IoBufferCopy));
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
  RegisterSystemEnv(registry);
  RegisterSystemPath(registry);
  RegisterSystemFs(registry);
  RegisterSystemIo(registry);
  return registry;
}

} // namespace Simple::VM::Native
