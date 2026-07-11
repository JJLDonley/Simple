#include "native/registry.h"

#include "runtime/abi.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "native/buffer.h"
#include "native/channel.h"
#include "native/env.h"
#include "native/fs.h"
#include "ffi/dl_runtime.h"
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

bool CloseFileResource(void* payload, std::string*) {
  if (!payload) return true;
  return std::fclose(static_cast<std::FILE*>(payload)) == 0;
}

std::FILE* GetFileFromRegistry(NativeCallContext& context, int32_t fd) {
  if (!context.resource_registry || !context.file_handles || fd < 0 ||
      static_cast<size_t>(fd) >= context.file_handles->size()) {
    return nullptr;
  }
  NativeResourceRecord* record = nullptr;
  const NativeResourceStatus status = context.resource_registry->Get(
      (*context.file_handles)[static_cast<size_t>(fd)], NativeResourceKind::File, &record);
  if (status != NativeResourceStatus::Ok || !record) return nullptr;
  return static_cast<std::FILE*>(record->payload);
}

Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

bool ReadStringArg(NativeCallContext& context, size_t index, std::string* out_value);
bool ReadByteSequence(NativeCallContext& context, size_t index, std::vector<int32_t>* out);
Slot CreateByteList(Heap& heap, const std::vector<uint32_t>& values);
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
  int64_t seed = 0;
  if (!context.ArgI64(0, &seed)) return NativeCallResult::Error("System.Random.seed missing seed");
  Random::Seed(static_cast<uint64_t>(seed));
  return NativeCallResult::Void();
}

NativeCallResult RandomI32(NativeCallContext&) {
  return NativeCallResult::I32(Random::I32());
}

NativeCallResult RandomI64(NativeCallContext&) {
  return NativeCallResult::I64(Random::I64());
}

NativeCallResult RandomFillBytes(NativeCallContext& context) {
  HeapObject* obj = context.heap && !context.args.empty()
                        ? context.heap->Get(UnpackRef(context.args[0]))
                        : nullptr;
  if (!Buffer::IsBuffer(obj)) return NativeCallResult::I32(0);
  std::vector<uint8_t> bytes(Buffer::Len(obj));
  Random::FillBytes(&bytes);
  for (uint32_t i = 0; i < bytes.size(); ++i) Buffer::WriteLE(obj, i, 1u, bytes[i]);
  return NativeCallResult::I32(1);
}

NativeCallResult RandomRange(NativeCallContext& context) {
  int32_t min_value = 0;
  int32_t max_value = 0;
  if (!context.ArgI32(0, &min_value) || !context.ArgI32(1, &max_value)) {
    return NativeCallResult::Error("System.Random.range missing bounds");
  }
  return NativeCallResult::I32(Random::Range(min_value, max_value));
}

NativeCallResult RandomF64(NativeCallContext&) {
  return NativeCallResult::F64(Random::F64());
}

NativeCallResult OsTimeMonoNs(NativeCallContext&) {
  return NativeCallResult::I64(Time::MonotonicNs());
}

NativeCallResult OsTimeWallNs(NativeCallContext&) {
  return NativeCallResult::I64(Time::WallNs());
}

NativeCallResult OsCwdGet(NativeCallContext&) {
  std::string cwd;
  if (!Os::CurrentWorkingDirectory(&cwd)) return NativeCallResult::Ref(HeapLayout::kNullRef);
  return NativeCallResult::String(std::move(cwd));
}

NativeCallResult OsPlatform(NativeCallContext&) {
  return NativeCallResult::String(Env::PlatformName());
}

NativeCallResult OsArch(NativeCallContext&) {
  return NativeCallResult::String(Env::ArchName());
}

NativeCallResult OsIsLinux(NativeCallContext&) {
#if defined(__linux__)
  return NativeCallResult::Bool(true);
#else
  return NativeCallResult::Bool(false);
#endif
}

NativeCallResult OsIsMacos(NativeCallContext&) {
#if defined(__APPLE__)
  return NativeCallResult::Bool(true);
#else
  return NativeCallResult::Bool(false);
#endif
}

NativeCallResult OsIsWindows(NativeCallContext&) {
#if defined(_WIN32)
  return NativeCallResult::Bool(true);
#else
  return NativeCallResult::Bool(false);
#endif
}

NativeCallResult OsPid(NativeCallContext&) {
#if defined(_WIN32)
  return NativeCallResult::I32(static_cast<int32_t>(GetCurrentProcessId()));
#else
  return NativeCallResult::I32(static_cast<int32_t>(getpid()));
#endif
}

NativeCallResult OsCpuCount(NativeCallContext&) {
  return NativeCallResult::I32(static_cast<int32_t>(std::thread::hardware_concurrency()));
}

NativeCallResult OsPageSize(NativeCallContext&) {
#if defined(_WIN32)
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  return NativeCallResult::I32(static_cast<int32_t>(info.dwPageSize));
#else
  return NativeCallResult::I32(static_cast<int32_t>(sysconf(_SC_PAGESIZE)));
#endif
}

NativeCallResult OsExit(NativeCallContext& context) {
  int32_t code = 0;
  if (!context.ArgI32(0, &code)) return NativeCallResult::Error("System.OS.exit missing code");
  std::exit(code);
}

NativeCallResult OsFormatWallNs(NativeCallContext& context) {
  int64_t ns = 0;
  if (!context.ArgI64(0, &ns)) return NativeCallResult::Error("System.OS.formatWallNs missing timestamp");
  return NativeCallResult::String(Time::FormatWallNsUtc(ns));
}

NativeCallResult OsSleepMs(NativeCallContext& context) {
  int32_t ms = 0;
  if (!context.ArgI32(0, &ms)) return NativeCallResult::Error("System.OS.sleepMs missing duration");
  Thread::SleepMs(ms);
  return NativeCallResult::Void();
}

NativeCallResult ThreadSleep(NativeCallContext& context) {
  int32_t ms = 0;
  if (!context.ArgI32(0, &ms)) return NativeCallResult::Error("System.Thread.sleep missing duration");
  Thread::SleepMs(ms);
  return NativeCallResult::Void();
}

NativeCallResult ThreadYield(NativeCallContext&) {
  Thread::Yield();
  return NativeCallResult::Void();
}

NativeCallResult ThreadHardwareConcurrency(NativeCallContext&) {
  return NativeCallResult::I32(Thread::HardwareConcurrency());
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

NativeCallResult EnvUnset(NativeCallContext& context) {
  std::string name;
  return NativeCallResult::Bool(ReadStringArg(context, 0, &name) && Env::Unset(name));
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

NativeCallResult PathSeparator(NativeCallContext&) {
  return NativeCallResult::String(Path::Separator());
}

NativeCallResult PathDelimiter(NativeCallContext&) {
  return NativeCallResult::String(Path::Delimiter());
}

NativeCallResult PathIsAbsolute(NativeCallContext& context) {
  std::string value;
  return NativeCallResult::Bool(ReadStringArg(context, 0, &value) && Path::IsAbsolute(value));
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

NativeCallResult PathStem(NativeCallContext& context) {
  NativeCallResult result;
  std::string value;
  if (!ReadStringArg(context, 0, &value)) {
    result.value = PackRef(HeapLayout::kNullRef);
    return result;
  }
  result.string_value = Path::Stem(value);
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
  if (!context.resource_registry || !context.file_handles || !ReadStringArg(context, 0, &path)) {
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
  NativeResourceRecord record;
  record.kind = NativeResourceKind::File;
  record.owned = true;
  record.debug_label = path;
  record.payload = file;
  record.close = CloseFileResource;
  const NativeHandleId handle = context.resource_registry->Insert(std::move(record));
  context.file_handles->push_back(handle);
  result.value = PackI32(static_cast<int32_t>(context.file_handles->size() - 1));
  return result;
}

NativeCallResult FsRead(NativeCallContext& context) {
  NativeCallResult result;
  const int32_t fd = UnpackI32(context.args[0]);
  const int32_t count = UnpackI32(context.args[2]);
  HeapObject* obj = GetHeapObject(context, 1);
  std::FILE* file = GetFileFromRegistry(context, fd);
  if (!file || count < 0 || !obj || obj->header.kind != ObjectKind::Array || obj->payload.size() < 4) {
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
  const size_t got = req > 0 ? std::fread(bytes.data(), 1, req, file) : 0;
  for (size_t i = 0; i < got; ++i) WriteU32(obj->payload, 4u + i * 4u, bytes[i]);
  result.value = PackI32(static_cast<int32_t>(got));
  return result;
}

NativeCallResult FsWrite(NativeCallContext& context) {
  NativeCallResult result;
  const int32_t fd = UnpackI32(context.args[0]);
  const int32_t count = UnpackI32(context.args[2]);
  HeapObject* obj = GetHeapObject(context, 1);
  std::FILE* file = GetFileFromRegistry(context, fd);
  if (!file || count < 0 || !obj || obj->header.kind != ObjectKind::Array || obj->payload.size() < 4) {
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
  const size_t wrote = req > 0 ? std::fwrite(bytes.data(), 1, req, file) : 0;
  result.value = PackI32(static_cast<int32_t>(wrote));
  return result;
}

NativeCallResult FsClose(NativeCallContext& context) {
  NativeCallResult result;
  result.has_value = false;
  const int32_t fd = UnpackI32(context.args[0]);
  if (!context.resource_registry || !context.file_handles || fd < 0 ||
      static_cast<size_t>(fd) >= context.file_handles->size()) {
    return result;
  }
  std::string ignored;
  context.resource_registry->Close((*context.file_handles)[static_cast<size_t>(fd)],
                                   NativeResourceKind::File,
                                   &ignored);
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

bool ReadByteSequence(NativeCallContext& context, size_t index, std::vector<int32_t>* out) {
  if (!out) return false;
  HeapObject* obj = GetHeapObject(context, index);
  if (!obj || obj->payload.size() < 4) return false;
  const uint32_t length = obj->payload[0] | (static_cast<uint32_t>(obj->payload[1]) << 8u) |
                          (static_cast<uint32_t>(obj->payload[2]) << 16u) |
                          (static_cast<uint32_t>(obj->payload[3]) << 24u);
  out->clear();
  out->reserve(length);
  if (obj->header.kind == ObjectKind::Bytes) {
    if (HeapLayout::kBytesDataOffset + static_cast<size_t>(length) > obj->payload.size()) return false;
    for (uint32_t i = 0; i < length; ++i) {
      out->push_back(static_cast<int32_t>(obj->payload[HeapLayout::BytesElementOffset(i)]));
    }
    return true;
  }
  if (obj->header.kind != ObjectKind::List && obj->header.kind != ObjectKind::Array) return false;
  const size_t elem_base = obj->header.kind == ObjectKind::List ? 8u : 4u;
  if (elem_base + static_cast<size_t>(length) * 4u > obj->payload.size()) return false;
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
  result.value = PackI32(ReadStringArg(context, 0, &path) && ReadByteSequence(context, 1, &values) &&
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
  if (const auto module = Simple::Lang::ParseLibraryImportPath(module_name)) {
    spec.library_module = Simple::Lang::LibraryModuleId{module->root, module->module_index};
  }
  spec.symbol_name = symbol_name;
  spec.parameter_types = std::move(params);
  spec.result_type = result_type;
  spec.layer = NativeLayer::System;
  spec.stability = NativeStability::Experimental;
  spec.doc_summary = std::string(module_name) + "." + symbol_name;
  if (result_type == Simple::Byte::TypeKind::String || result_type == Simple::Byte::TypeKind::Ref) {
    spec.allocation = NativeAllocationBehavior::MayAllocateVm;
    spec.gc_behavior = NativeGcBehavior::MaySafepoint;
  }
  spec.handler = std::move(handler);
  return spec;
}

NativeOwnershipRule DefaultOwnershipForAccess(NativeResourceAccess access) {
  switch (access) {
    case NativeResourceAccess::Input:
      return NativeOwnershipRule::Borrow;
    case NativeResourceAccess::Output:
      return NativeOwnershipRule::TransferToCaller;
    case NativeResourceAccess::InputOutput:
      return NativeOwnershipRule::TransferToCallee;
  }
  return NativeOwnershipRule::None;
}

NativeCleanupBehavior DefaultCleanupForAccess(NativeResourceAccess access) {
  switch (access) {
    case NativeResourceAccess::Input:
      return NativeCleanupBehavior::None;
    case NativeResourceAccess::Output:
      return NativeCleanupBehavior::AutoCloseOnVmShutdown;
    case NativeResourceAccess::InputOutput:
      return NativeCleanupBehavior::CloseRequired;
  }
  return NativeCleanupBehavior::None;
}

NativeFunctionSpec WithResource(NativeFunctionSpec spec,
                                NativeResourceKind kind,
                                NativeResourceAccess access,
                                uint32_t parameter_index = 0xffffffffu) {
  spec.resources.push_back(NativeResourceUse{kind, access, DefaultOwnershipForAccess(access),
                                             DefaultCleanupForAccess(access), parameter_index});
  return spec;
}

NativeFunctionSpec MayBlock(NativeFunctionSpec spec) {
  spec.blocking = NativeBlockingBehavior::MayBlock;
  return spec;
}

NativeFunctionSpec WithCapability(NativeFunctionSpec spec, const char* capability) {
  spec.capability_tags.push_back(capability);
  return spec;
}

NativeFunctionSpec WithStability(NativeFunctionSpec spec, NativeStability stability) {
  spec.stability = stability;
  return spec;
}

NativeFunctionSpec WithDoc(NativeFunctionSpec spec, const char* summary) {
  spec.doc_summary = summary;
  return spec;
}

} // namespace

bool NativeCallContext::ArgBool(size_t index, bool* out) const {
  if (!out || index >= args.size()) return false;
  const uint64_t value = static_cast<uint64_t>(UnpackI32(args[index]));
  if (!Simple::VM::Runtime::IsValidAbiScalarValue(Simple::Byte::TypeKind::Bool, value)) {
    return false;
  }
  *out = value != 0;
  return true;
}

bool NativeCallContext::ArgChar(size_t index, uint32_t* out) const {
  if (!out || index >= args.size()) return false;
  const uint64_t value = static_cast<uint64_t>(UnpackI32(args[index]));
  if (!Simple::VM::Runtime::IsValidAbiScalarValue(Simple::Byte::TypeKind::Char, value)) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

bool NativeCallContext::ArgI32(size_t index, int32_t* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackI32(args[index]);
  return true;
}

bool NativeCallContext::ArgI64(size_t index, int64_t* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackI64(args[index]);
  return true;
}

bool NativeCallContext::ArgF32(size_t index, float* out) const {
  if (!out || index >= args.size()) return false;
  *out = BitsToF32(UnpackU32Bits(args[index]));
  return true;
}

bool NativeCallContext::ArgF64(size_t index, double* out) const {
  if (!out || index >= args.size()) return false;
  *out = BitsToF64(UnpackU64Bits(args[index]));
  return true;
}

bool NativeCallContext::ArgRef(size_t index, uint32_t* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackRef(args[index]);
  return true;
}

bool NativeCallContext::ArgHandle(size_t index, NativeHandleId* out) const {
  if (!out || index >= args.size()) return false;
  *out = UnpackNativeHandleId(args[index]);
  return true;
}

NativeResourceStatus NativeCallContext::ArgResourceHandle(size_t index,
                                                         NativeResourceKind expected_kind,
                                                         NativeHandleId* out_handle,
                                                         NativeResourceRecord** out_record) const {
  if (out_handle) *out_handle = NativeHandleId{};
  if (out_record) *out_record = nullptr;
  if (!resource_registry || index >= args.size()) return NativeResourceStatus::InvalidHandle;
  const NativeHandleId handle = UnpackNativeHandleId(args[index]);
  if (out_handle) *out_handle = handle;
  return resource_registry->Get(handle, expected_kind, out_record);
}

bool NativeCallContext::ArgBytesView(size_t index, Simple::VM::Runtime::SimpleBytesView* out) const {
  if (!out || !heap || index >= args.size()) return false;
  const uint32_t ref = UnpackRef(args[index]);
  const HeapObject* obj = heap->Get(ref);
  if (!obj || obj->header.kind != ObjectKind::Bytes ||
      obj->payload.size() < HeapLayout::kBytesDataOffset) {
    return false;
  }
  const uint32_t length = ReadU32Payload(obj->payload, HeapLayout::kBytesLengthOffset);
  if (HeapLayout::kBytesDataOffset + static_cast<size_t>(length) > obj->payload.size()) {
    return false;
  }
  out->data = length == 0 ? nullptr : obj->payload.data() + HeapLayout::kBytesDataOffset;
  out->size = length;
  return true;
}

bool NativeCallContext::ArgString(size_t index, std::string* out) {
  return ReadStringArg(*this, index, out);
}

bool NativeCallContext::ArgStringView(size_t index, Simple::VM::Runtime::SimpleStringView* out) {
  if (!out) return false;
  std::string value;
  if (!ArgString(index, &value)) return false;
  borrowed_string_storage.push_back(std::move(value));
  const std::string& stored = borrowed_string_storage.back();
  out->data = stored.empty() ? nullptr : stored.data();
  out->size = stored.size();
  out->encoding = Simple::VM::Runtime::AbiStringEncoding::Utf8;
  return true;
}

NativeCallResult NativeCallResult::Void() {
  NativeCallResult result;
  result.has_value = false;
  return result;
}

NativeCallResult NativeCallResult::Bool(bool value) {
  NativeCallResult result;
  result.value = PackI32(value ? 1 : 0);
  return result;
}

NativeCallResult NativeCallResult::Char(uint32_t value) {
  NativeCallResult result;
  if (!Simple::VM::Runtime::IsValidAbiScalarValue(Simple::Byte::TypeKind::Char, value)) {
    result.ok = false;
    result.has_value = false;
    result.error = "invalid char ABI value";
    return result;
  }
  result.value = PackI32(static_cast<int32_t>(value));
  return result;
}

NativeCallResult NativeCallResult::I32(int32_t value) {
  NativeCallResult result;
  result.value = PackI32(value);
  return result;
}

NativeCallResult NativeCallResult::I64(int64_t value) {
  NativeCallResult result;
  result.value = PackI64(value);
  return result;
}

NativeCallResult NativeCallResult::F32(float value) {
  NativeCallResult result;
  result.value = PackF32(value);
  return result;
}

NativeCallResult NativeCallResult::F64(double value) {
  NativeCallResult result;
  result.value = PackF64(value);
  return result;
}

NativeCallResult NativeCallResult::Ref(uint32_t value) {
  NativeCallResult result;
  result.value = PackRef(value);
  return result;
}

NativeCallResult NativeCallResult::Handle(NativeHandleId value) {
  NativeCallResult result;
  result.value = PackNativeHandleId(value);
  return result;
}

NativeCallResult NativeCallResult::String(std::string value) {
  NativeCallResult result;
  result.string_value = std::move(value);
  return result;
}

NativeCallResult NativeCallResult::Error(std::string message) {
  NativeCallResult result;
  result.ok = false;
  result.has_value = false;
  result.error = std::move(message);
  return result;
}

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

const NativeFunctionSpec* NativeRegistry::Find(Simple::Lang::LibraryModuleId module,
                                               const std::string& symbol_name) const {
  for (const NativeFunctionSpec& spec : functions_) {
    if (spec.library_module && *spec.library_module == module && spec.symbol_name == symbol_name) return &spec;
  }
  return nullptr;
}

const std::vector<NativeFunctionSpec>& NativeRegistry::Functions() const {
  return functions_;
}

size_t NativeRegistry::Size() const {
  return functions_.size();
}

bool IsDirectNativeBindingSafe(const NativeFunctionSpec& spec) {
  return spec.direct_binding_safe && spec.blocking == NativeBlockingBehavior::NonBlocking &&
         spec.allocation == NativeAllocationBehavior::NoAllocation &&
         spec.gc_behavior == NativeGcBehavior::NoSafepoint && spec.resources.empty();
}

bool NativeTypeMatchesLibraryType(Simple::Byte::TypeKind native_type,
                                  std::string_view library_type) {
  using Simple::Byte::TypeKind;
  switch (native_type) {
    case TypeKind::Unspecified:
    case TypeKind::Void: return library_type == "void";
    case TypeKind::Bool: return library_type == "bool";
    case TypeKind::I8: return library_type == "i8";
    case TypeKind::I16: return library_type == "i16";
    case TypeKind::I32: return library_type == "i32" || library_type == "bool";
    case TypeKind::I64: return library_type == "i64";
    case TypeKind::I128: return library_type == "i128";
    case TypeKind::U8: return library_type == "u8";
    case TypeKind::U16: return library_type == "u16";
    case TypeKind::U32: return library_type == "u32";
    case TypeKind::U64: return library_type == "u64";
    case TypeKind::U128: return library_type == "u128";
    case TypeKind::F32: return library_type == "f32";
    case TypeKind::F64: return library_type == "f64";
    case TypeKind::String: return library_type == "string";
    case TypeKind::Ref:
    case TypeKind::Array:
    case TypeKind::List: return library_type == "ref" || library_type == "i32[]" || library_type == "string[]";
    case TypeKind::Ptr: return library_type == "ptr" || library_type == "i64" || library_type == "handle";
    case TypeKind::Char: return library_type == "char";
    case TypeKind::Never: return library_type == "never";
    case TypeKind::Function: return library_type == "function";
    case TypeKind::Result: return library_type == "result";
    case TypeKind::Option: return library_type == "option";
    case TypeKind::Vector: return library_type == "vector";
  }
  return false;
}

bool ValidateNativeSpecAgainstLibrarySignature(const NativeFunctionSpec& spec,
                                               const Simple::Lang::LibrarySignatureSpec& signature,
                                               std::string* error) {
  const std::string name = spec.module_name + "." + spec.symbol_name;
  if (spec.parameter_types.size() != signature.params.size()) {
    if (error) *error = name + " catalog signature arity mismatch";
    return false;
  }
  for (size_t i = 0; i < spec.parameter_types.size(); ++i) {
    if (!NativeTypeMatchesLibraryType(spec.parameter_types[i], signature.params[i].type.name)) {
      if (error) *error = name + " catalog signature parameter mismatch at " + std::to_string(i);
      return false;
    }
  }
  if (!NativeTypeMatchesLibraryType(spec.result_type, signature.return_type.name)) {
    if (error) *error = name + " catalog signature result mismatch";
    return false;
  }
  return true;
}

NativeJitCallValidation AnalyzeNativeJitCall(const NativeFunctionSpec& spec,
                                             const std::vector<Simple::Byte::TypeKind>& parameter_types,
                                             Simple::Byte::TypeKind result_type) {
  NativeJitCallValidation out;
  out.may_block = spec.blocking != NativeBlockingBehavior::NonBlocking;
  out.may_allocate = spec.allocation != NativeAllocationBehavior::NoAllocation;
  auto needs_root = [](Simple::Byte::TypeKind kind) {
    return kind == Simple::Byte::TypeKind::String || kind == Simple::Byte::TypeKind::Ref;
  };
  for (Simple::Byte::TypeKind kind : parameter_types) {
    if (needs_root(kind)) out.needs_roots = true;
  }
  if (needs_root(result_type)) out.needs_roots = true;

  std::string metadata_error;
  out.metadata_valid = ValidateNativeFunctionMetadata(spec, &metadata_error);
  if (!out.metadata_valid) {
    out.reason = "invalid-native-abi-metadata";
    return out;
  }
  if (spec.parameter_types.size() != parameter_types.size()) {
    out.reason = "metadata-signature-mismatch";
    return out;
  }
  for (size_t i = 0; i < parameter_types.size(); ++i) {
    if (spec.parameter_types[i] != parameter_types[i]) {
      out.reason = "metadata-signature-mismatch";
      return out;
    }
  }
  if (spec.result_type != Simple::Byte::TypeKind::Unspecified && spec.result_type != result_type) {
    out.reason = "metadata-signature-mismatch";
    return out;
  }
  out.signature_matches = true;
  out.jit_helper_safe = true;

  for (const NativeResourceUse& resource : spec.resources) {
    if (resource.access != NativeResourceAccess::Input || resource.parameter_index >= spec.parameter_types.size()) {
      out.reason = "resource-argument-or-result";
      return out;
    }
  }
  if (out.may_block) {
    out.reason = "blocking-call";
    return out;
  }
  if (out.may_allocate) {
    out.reason = "allocating-call";
    return out;
  }
  if (spec.gc_behavior != NativeGcBehavior::NoSafepoint) {
    out.reason = "gc-safepoint-call";
    return out;
  }
  out.jit_loop_safe = true;
  return out;
}

bool ValidateNativeFunctionMetadata(const NativeFunctionSpec& spec, std::string* error) {
  const std::string name = spec.module_name + "." + spec.symbol_name;
  if (spec.module_name.empty() || spec.symbol_name.empty()) {
    if (error) *error = "native metadata has empty module or symbol";
    return false;
  }
  if (!spec.handler) {
    if (error) *error = name + " missing handler";
    return false;
  }
  if (spec.module_name.rfind("System.", 0) == 0 && spec.layer != NativeLayer::System) {
    if (error) *error = name + " system native function must declare system layer";
    return false;
  }
  if (spec.library_module) {
    const auto parsed_module = Simple::Lang::ParseLibraryImportPath(spec.module_name);
    if (!parsed_module) {
      if (error) *error = name + " native module catalog id has non-catalog module name";
      return false;
    }
    const Simple::Lang::LibraryModuleId expected{parsed_module->root, parsed_module->module_index};
    if (!(*spec.library_module == expected)) {
      if (error) *error = name + " native module catalog id mismatch";
      return false;
    }
    const auto signature = Simple::Lang::GetLibrarySignature(*spec.library_module, spec.symbol_name);
    if (signature && !ValidateNativeSpecAgainstLibrarySignature(spec, *signature, error)) {
      return false;
    }
  }
  if (spec.doc_summary.empty()) {
    if (error) *error = name + " missing doc summary";
    return false;
  }
  std::string abi_error;
  if (!Simple::VM::Runtime::ValidateAbiCallableSignature(spec.parameter_types, spec.result_type,
                                                         false, &abi_error)) {
    if (error) *error = name + " has invalid ABI signature: " + abi_error;
    return false;
  }
  if (spec.allocation == NativeAllocationBehavior::MayAllocateVm &&
      spec.gc_behavior != NativeGcBehavior::MaySafepoint) {
    if (error) *error = name + " VM allocation metadata must declare GC safepoint behavior";
    return false;
  }
  if (spec.direct_binding_safe && !IsDirectNativeBindingSafe(spec)) {
    if (error) *error = name + " direct native binding marked safe with unsafe metadata";
    return false;
  }
  for (const NativeResourceUse& resource : spec.resources) {
    if (resource.kind == NativeResourceKind::Unknown) {
      if (error) *error = name + " declares unknown resource kind";
      return false;
    }
    if (resource.access != NativeResourceAccess::Output &&
        resource.parameter_index >= spec.parameter_types.size()) {
      if (error) *error = name + " resource parameter index out of range";
      return false;
    }
    if (resource.ownership == NativeOwnershipRule::None) {
      if (error) *error = name + " resource missing ownership rule";
      return false;
    }
    if (resource.access == NativeResourceAccess::Output &&
        resource.cleanup == NativeCleanupBehavior::None) {
      if (error) *error = name + " resource output missing cleanup behavior";
      return false;
    }
    if (resource.access == NativeResourceAccess::InputOutput &&
        resource.cleanup != NativeCleanupBehavior::CloseRequired) {
      if (error) *error = name + " inout resource must require close";
      return false;
    }
  }
  return true;
}

bool ValidateNativeRegistryMetadata(const NativeRegistry& registry, std::string* error) {
  for (const NativeFunctionSpec& spec : registry.Functions()) {
    if (!ValidateNativeFunctionMetadata(spec, error)) return false;
  }
  return true;
}

std::string LayerMarkdown(NativeLayer layer) {
  switch (layer) {
    case NativeLayer::Core:
      return "core";
    case NativeLayer::System:
      return "system";
    case NativeLayer::Standard:
      return "standard";
    case NativeLayer::Domain:
      return "domain";
  }
  return "unknown";
}

std::string TypeKindMarkdown(Simple::Byte::TypeKind kind) {
  using Simple::Byte::TypeKind;
  switch (kind) {
    case TypeKind::Unspecified:
      return "void";
    case TypeKind::Bool:
      return "bool";
    case TypeKind::I32:
      return "i32";
    case TypeKind::I64:
      return "i64";
    case TypeKind::F32:
      return "f32";
    case TypeKind::F64:
      return "f64";
    case TypeKind::String:
      return "string";
    case TypeKind::Ref:
      return "ref";
    default:
      return "unknown";
  }
}

std::string BlockingMarkdown(NativeBlockingBehavior blocking) {
  return blocking == NativeBlockingBehavior::MayBlock ? "may-block" : "non-blocking";
}

std::string AllocationMarkdown(NativeAllocationBehavior allocation) {
  switch (allocation) {
    case NativeAllocationBehavior::NoAllocation:
      return "no-alloc";
    case NativeAllocationBehavior::MayAllocateVm:
      return "vm-alloc";
    case NativeAllocationBehavior::MayAllocateHost:
      return "host-alloc";
  }
  return "unknown";
}

std::string GcMarkdown(NativeGcBehavior gc_behavior) {
  switch (gc_behavior) {
    case NativeGcBehavior::NoSafepoint:
      return "no-safepoint";
    case NativeGcBehavior::MaySafepoint:
      return "may-safepoint";
  }
  return "unknown";
}

std::string DirectBindingMarkdown(bool direct_binding_safe) {
  return direct_binding_safe ? "safe" : "-";
}

std::string StabilityMarkdown(NativeStability stability) {
  switch (stability) {
    case NativeStability::Experimental:
      return "experimental";
    case NativeStability::Stable:
      return "stable";
    case NativeStability::Unsafe:
      return "unsafe";
  }
  return "unknown";
}

std::string ResourceKindMarkdown(NativeResourceKind kind) {
  switch (kind) {
    case NativeResourceKind::File:
      return "file";
    case NativeResourceKind::Directory:
      return "directory";
    case NativeResourceKind::Socket:
      return "socket";
    case NativeResourceKind::Listener:
      return "listener";
    case NativeResourceKind::Process:
      return "process";
    case NativeResourceKind::Thread:
      return "thread";
    case NativeResourceKind::Job:
      return "job";
    case NativeResourceKind::Channel:
      return "channel";
    case NativeResourceKind::FfiLibrary:
      return "ffi-library";
    case NativeResourceKind::FfiSymbol:
      return "ffi-symbol";
    case NativeResourceKind::AsmUnit:
      return "asm-unit";
    case NativeResourceKind::AsmObject:
      return "asm-object";
    case NativeResourceKind::AsmSymbol:
      return "asm-symbol";
    case NativeResourceKind::Buffer:
      return "buffer";
    case NativeResourceKind::Timer:
      return "timer";
    case NativeResourceKind::Watcher:
      return "watcher";
    case NativeResourceKind::Terminal:
      return "terminal";
    case NativeResourceKind::JsonValue:
      return "json-value";
    case NativeResourceKind::Logger:
      return "logger";
    case NativeResourceKind::Random:
      return "random";
    case NativeResourceKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string ResourceAccessMarkdown(NativeResourceAccess access) {
  switch (access) {
    case NativeResourceAccess::Input:
      return "in";
    case NativeResourceAccess::Output:
      return "out";
    case NativeResourceAccess::InputOutput:
      return "inout";
  }
  return "unknown";
}

std::string OwnershipMarkdown(NativeOwnershipRule ownership) {
  switch (ownership) {
    case NativeOwnershipRule::None:
      return "none";
    case NativeOwnershipRule::Borrow:
      return "borrow";
    case NativeOwnershipRule::TransferToCaller:
      return "to-caller";
    case NativeOwnershipRule::TransferToCallee:
      return "to-callee";
  }
  return "unknown";
}

std::string CleanupMarkdown(NativeCleanupBehavior cleanup) {
  switch (cleanup) {
    case NativeCleanupBehavior::None:
      return "none";
    case NativeCleanupBehavior::CloseRequired:
      return "close-required";
    case NativeCleanupBehavior::AutoCloseOnVmShutdown:
      return "vm-shutdown";
  }
  return "unknown";
}

std::string TagsMarkdown(const std::vector<std::string>& tags) {
  if (tags.empty()) return "-";
  std::ostringstream out;
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i > 0) out << ", ";
    out << tags[i];
  }
  return out.str();
}

std::string PlatformsMarkdown(const std::vector<std::string>& platforms) {
  return platforms.empty() ? "all" : TagsMarkdown(platforms);
}

std::string SummaryMarkdown(const std::string& summary) {
  return summary.empty() ? "-" : summary;
}

std::string AvailabilityMarkdown(Simple::Lang::LibraryApiAvailability availability) {
  switch (availability) {
    case Simple::Lang::LibraryApiAvailability::Implemented: return "implemented";
    case Simple::Lang::LibraryApiAvailability::Planned: return "planned";
  }
  return "planned";
}

std::string ResourcesMarkdown(const std::vector<NativeResourceUse>& resources) {
  if (resources.empty()) return "-";
  std::ostringstream out;
  for (size_t i = 0; i < resources.size(); ++i) {
    if (i > 0) out << ", ";
    const NativeResourceUse& resource = resources[i];
    out << ResourceAccessMarkdown(resource.access) << ":" << ResourceKindMarkdown(resource.kind);
    if (resource.parameter_index != 0xffffffffu) out << "@" << resource.parameter_index;
    out << ":" << OwnershipMarkdown(resource.ownership) << ":" << CleanupMarkdown(resource.cleanup);
  }
  return out.str();
}

std::string GenerateStdLibMarkdown(const NativeRegistry& registry) {
  std::map<std::string, std::vector<const NativeFunctionSpec*>> modules;
  for (const NativeFunctionSpec& spec : registry.Functions()) {
    const std::string module_key = spec.library_module
                                       ? std::string(Simple::Lang::ToImportPath(*spec.library_module))
                                       : spec.module_name;
    modules[module_key].push_back(&spec);
  }
  std::ostringstream out;
  out << "# Native Standard Library Metadata\n\n";
  out << "Generated from `NativeRegistry` metadata plus library catalog module/member ids.\n";
  for (auto& entry : modules) {
    std::sort(entry.second.begin(), entry.second.end(), [](const NativeFunctionSpec* lhs,
                                                           const NativeFunctionSpec* rhs) {
      return lhs->symbol_name < rhs->symbol_name;
    });
    out << "\n## " << entry.first << "\n\n";
    out << "| Symbol | Catalog | Availability | Layer | Signature | Blocking | Allocation | GC | Direct | Capabilities | Resources | Platforms | Stability | Summary |\n"
        << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const NativeFunctionSpec* spec : entry.second) {
      const auto catalog_meta = spec->library_module
                                    ? Simple::Lang::GetLibraryMemberMetadata(*spec->library_module, spec->symbol_name)
                                    : Simple::Lang::LibraryMemberMetadata{};
      out << "| `" << spec->symbol_name << "` | `"
          << (spec->library_module ? Simple::Lang::ToImportPath(*spec->library_module) : std::string_view("-"))
          << "` | `"
          << (spec->library_module ? AvailabilityMarkdown(catalog_meta.availability) : std::string("-"))
          << "` | `" << LayerMarkdown(spec->layer) << "` | `(";
      for (size_t i = 0; i < spec->parameter_types.size(); ++i) {
        if (i > 0) out << ", ";
        out << TypeKindMarkdown(spec->parameter_types[i]);
      }
      out << ") -> " << TypeKindMarkdown(spec->result_type) << "` | `"
          << BlockingMarkdown(spec->blocking) << "` | `"
          << AllocationMarkdown(spec->allocation) << "` | `"
          << GcMarkdown(spec->gc_behavior) << "` | `"
          << DirectBindingMarkdown(spec->direct_binding_safe) << "` | `"
          << TagsMarkdown(spec->capability_tags) << "` | `"
          << ResourcesMarkdown(spec->resources) << "` | `"
          << PlatformsMarkdown(spec->platforms) << "` | `"
          << StabilityMarkdown(spec->stability) << "` | "
          << SummaryMarkdown(spec->doc_summary) << " |\n";
    }
  }
  return out.str();
}

void RegisterSystemRandom(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(WithCapability(MakeSpec("System.Random", "seed", {TypeKind::I64},
                                            TypeKind::Unspecified, RandomSeed),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec("System.Random", "i32", {}, TypeKind::I32,
                                            RandomI32),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec("System.Random", "i64", {}, TypeKind::I64,
                                            RandomI64),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec("System.Random", "fillBytes", {TypeKind::Ref},
                                            TypeKind::I32, RandomFillBytes),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec("System.Random", "range",
                                            {TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                                            RandomRange),
                                   "randomness"));
  registry.Register(WithCapability(MakeSpec("System.Random", "f64", {}, TypeKind::F64,
                                            RandomF64),
                                   "randomness"));
}

void RegisterSystemOs(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(WithCapability(MakeSpec("System.OS", "args_count", {}, TypeKind::I32, EnvArgsCount),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec("System.OS", "args_get", {TypeKind::I32}, TypeKind::String, EnvArg),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec("System.OS", "env_get", {TypeKind::String}, TypeKind::String, EnvGet),
                                   "environment.read"));
  registry.Register(WithCapability(MakeSpec("System.OS", "cwd_get", {}, TypeKind::String, OsCwdGet),
                                   "filesystem.read"));
  registry.Register(WithCapability(MakeSpec("System.OS", "time_mono_ns", {}, TypeKind::I64, OsTimeMonoNs),
                                   "clock.time"));
  registry.Register(WithCapability(MakeSpec("System.OS", "time_wall_ns", {}, TypeKind::I64, OsTimeWallNs),
                                   "clock.time"));
  registry.Register(WithStability(MakeSpec("System.OS", "platform", {}, TypeKind::String, OsPlatform),
                                  NativeStability::Stable));
  registry.Register(WithStability(MakeSpec("System.OS", "arch", {}, TypeKind::String, OsArch),
                                  NativeStability::Stable));
  registry.Register(MakeSpec("System.OS", "isLinux", {}, TypeKind::Bool, OsIsLinux));
  registry.Register(MakeSpec("System.OS", "isMacos", {}, TypeKind::Bool, OsIsMacos));
  registry.Register(MakeSpec("System.OS", "isWindows", {}, TypeKind::Bool, OsIsWindows));
  registry.Register(MakeSpec("System.OS", "pid", {}, TypeKind::I32, OsPid));
  registry.Register(MakeSpec("System.OS", "cpuCount", {}, TypeKind::I32, OsCpuCount));
  registry.Register(MakeSpec("System.OS", "pageSize", {}, TypeKind::I32, OsPageSize));
  registry.Register(MakeSpec("System.OS", "exit", {TypeKind::I32}, TypeKind::Unspecified, OsExit));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.OS", "sleepMs", {TypeKind::I32},
                                                     TypeKind::Unspecified, OsSleepMs)),
                                   "threading"));
  registry.Register(WithCapability(MakeSpec("System.OS", "formatWallNs", {TypeKind::I64},
                                            TypeKind::String, OsFormatWallNs),
                                   "clock.time"));
}

void RegisterSystemThread(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(WithCapability(MayBlock(MakeSpec("System.Thread", "sleep", {TypeKind::I32},
                                                     TypeKind::Unspecified, ThreadSleep)),
                                   "threading"));
  registry.Register(WithCapability(MakeSpec("System.Thread", "yield", {}, TypeKind::Unspecified,
                                            ThreadYield),
                                   "threading"));
  registry.Register(WithCapability(MakeSpec("System.Thread", "hardwareConcurrency", {},
                                            TypeKind::I32, ThreadHardwareConcurrency),
                                   "threading"));
}

void RegisterSystemJson(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.Json", "parse", {TypeKind::String}, TypeKind::I64,
                             JsonParse));
  registry.Register(MakeSpec("System.Json", "stringify", {TypeKind::I64}, TypeKind::String,
                             JsonStringify));
  registry.Register(MakeSpec("System.Json", "free", {TypeKind::I64}, TypeKind::I32, JsonFree));
}

void RegisterSystemDl(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(WithDoc(
      WithStability(
          WithResource(WithCapability(MakeSpec("System.FFI", "open", {TypeKind::String},
                                               TypeKind::I64, DlOpen),
                                      "ffi.dynamic_load"),
                       NativeResourceKind::FfiLibrary, NativeResourceAccess::Output),
          NativeStability::Unsafe),
      "Open a dynamic library handle."));
  registry.Register(WithDoc(
      WithStability(
          WithResource(WithCapability(MakeSpec("System.FFI", "sym",
                                               {TypeKind::I64, TypeKind::String}, TypeKind::I64,
                                               DlSymbol),
                                      "ffi.dynamic_load"),
                       NativeResourceKind::FfiLibrary, NativeResourceAccess::Input, 0),
          NativeStability::Unsafe),
      "Resolve a symbol from a dynamic library handle."));
  registry.Register(WithStability(
      WithResource(WithCapability(MakeSpec("System.FFI", "close", {TypeKind::I64},
                                           TypeKind::I32, DlClose),
                                  "ffi.dynamic_load"),
                   NativeResourceKind::FfiLibrary, NativeResourceAccess::InputOutput, 0),
      NativeStability::Unsafe));
  registry.Register(WithStability(MakeSpec("System.FFI", "last_error", {}, TypeKind::String,
                                           DlLastError),
                                  NativeStability::Unsafe));
}

void RegisterSystemLog(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.Log", "setLevel", {TypeKind::I32}, TypeKind::Unspecified,
                             LogSetLevel));
  registry.Register(MakeSpec("System.Log", "setFile", {TypeKind::String}, TypeKind::I32,
                             LogSetFile));
  registry.Register(MakeSpec("System.Log", "log", {TypeKind::I32, TypeKind::String},
                             TypeKind::Unspecified, LogEmit));
  registry.Register(MakeSpec("System.Log", "flush", {}, TypeKind::Bool, LogFlush));
  registry.Register(MakeSpec("System.Log", "info", {TypeKind::String}, TypeKind::Unspecified,
                             LogInfo));
  registry.Register(MakeSpec("System.Log", "warn", {TypeKind::String}, TypeKind::Unspecified,
                             LogWarn));
  registry.Register(MakeSpec("System.Log", "error", {TypeKind::String}, TypeKind::Unspecified,
                             LogError));
}

void RegisterSystemPath(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.Path", "separator", {}, TypeKind::String, PathSeparator));
  registry.Register(MakeSpec("System.Path", "delimiter", {}, TypeKind::String, PathDelimiter));
  registry.Register(MakeSpec("System.Path", "isAbsolute", {TypeKind::String}, TypeKind::Bool,
                             PathIsAbsolute));
  registry.Register(MakeSpec("System.Path", "join", {TypeKind::String, TypeKind::String},
                             TypeKind::String, PathJoin));
  registry.Register(MakeSpec("System.Path", "dirname", {TypeKind::String}, TypeKind::String,
                             PathDirname));
  registry.Register(MakeSpec("System.Path", "basename", {TypeKind::String}, TypeKind::String,
                             PathBasename));
  registry.Register(MakeSpec("System.Path", "ext", {TypeKind::String}, TypeKind::String,
                             PathExt));
  registry.Register(MakeSpec("System.Path", "stem", {TypeKind::String}, TypeKind::String,
                             PathStem));
  registry.Register(MakeSpec("System.Path", "normalize", {TypeKind::String}, TypeKind::String,
                             PathNormalize));
  registry.Register(WithCapability(MakeSpec("System.Path", "exists", {TypeKind::String},
                                            TypeKind::I32, PathExists),
                                   "filesystem.read"));
  registry.Register(WithCapability(MakeSpec("System.Path", "isFile", {TypeKind::String},
                                            TypeKind::I32, PathIsFile),
                                   "filesystem.read"));
  registry.Register(WithCapability(MakeSpec("System.Path", "isDir", {TypeKind::String},
                                            TypeKind::I32, PathIsDir),
                                   "filesystem.read"));
}

void RegisterSystemFs(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(WithDoc(WithCapability(MayBlock(MakeSpec("System.FS", "readText",
                                                               {TypeKind::String},
                                                               TypeKind::String, FsReadText)),
                                            "filesystem.read"),
                            "Read a UTF-8 text file."));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "writeText",
                                                  {TypeKind::String, TypeKind::String},
                                                  TypeKind::I32, FsWriteText)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "readBytes",
                                                  {TypeKind::String}, TypeKind::Ref, FsReadBytes)),
                                   "filesystem.read"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "writeBytes",
                                                  {TypeKind::String, TypeKind::Ref},
                                                  TypeKind::I32, FsWriteBytes)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "listDir",
                                                  {TypeKind::String}, TypeKind::Ref, FsListDir)),
                                   "filesystem.read"));
  registry.Register(WithDoc(
      WithResource(WithCapability(MayBlock(MakeSpec("System.FS", "open",
                                                   {TypeKind::String, TypeKind::I32},
                                                   TypeKind::I32, FsOpen)),
                                  "filesystem.open"),
                   NativeResourceKind::File, NativeResourceAccess::Output),
      "Open a file descriptor handle."));
  registry.Register(WithResource(
      WithCapability(MayBlock(MakeSpec("System.FS", "read",
                                       {TypeKind::I32, TypeKind::Ref, TypeKind::I32},
                                       TypeKind::I32, FsRead)),
                     "filesystem.read"),
      NativeResourceKind::File, NativeResourceAccess::Input, 0));
  registry.Register(WithResource(
      WithCapability(MayBlock(MakeSpec("System.FS", "write",
                                       {TypeKind::I32, TypeKind::Ref, TypeKind::I32},
                                       TypeKind::I32, FsWrite)),
                     "filesystem.write"),
      NativeResourceKind::File, NativeResourceAccess::Input, 0));
  registry.Register(WithResource(MakeSpec("System.FS", "close", {TypeKind::I32},
                                         TypeKind::Unspecified, FsClose),
                                 NativeResourceKind::File, NativeResourceAccess::InputOutput, 0));
  registry.Register(WithCapability(MakeSpec("System.FS", "cwd", {}, TypeKind::String, FsCwd),
                                   "filesystem.read"));
  registry.Register(WithCapability(
      WithCapability(MayBlock(MakeSpec("System.FS", "copy",
                                       {TypeKind::String, TypeKind::String}, TypeKind::I32,
                                       FsCopy)),
                     "filesystem.read"),
      "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "remove", {TypeKind::String},
                                                   TypeKind::I32, FsRemove)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "mkdir", {TypeKind::String},
                                                   TypeKind::I32, FsMkdir)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "mkdirAll", {TypeKind::String},
                                                   TypeKind::I32, FsMkdirAll)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec("System.FS", "setCwd", {TypeKind::String},
                                                   TypeKind::I32, FsSetCwd)),
                                   "filesystem.write"));
}

void RegisterSystemEnv(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(WithCapability(MakeSpec("System.Env", "argsCount", {}, TypeKind::I32,
                                            EnvArgsCount),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec("System.Env", "arg", {TypeKind::I32},
                                            TypeKind::String, EnvArg),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec("System.Env", "get", {TypeKind::String},
                                            TypeKind::String, EnvGet),
                                   "environment.read"));
  registry.Register(WithCapability(MakeSpec("System.Env", "set",
                                            {TypeKind::String, TypeKind::String}, TypeKind::I32,
                                            EnvSet),
                                   "environment.write"));
  registry.Register(WithCapability(MakeSpec("System.Env", "unset", {TypeKind::String},
                                            TypeKind::Bool, EnvUnset),
                                   "environment.write"));
  registry.Register(WithCapability(MakeSpec("System.Env", "exePath", {}, TypeKind::String,
                                            EnvExePath),
                                   "process.args"));
}

void RegisterSystemIo(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.IO", "buffer_new", {TypeKind::I32}, TypeKind::Ref,
                             IoBufferNew));
  registry.Register(MakeSpec("System.IO", "buffer_len", {TypeKind::Ref}, TypeKind::I32,
                             IoBufferLen));
  registry.Register(MakeSpec("System.IO", "buffer_fill", {TypeKind::Ref, TypeKind::I32,
                                                            TypeKind::I32},
                             TypeKind::I32, IoBufferFill));
  registry.Register(MakeSpec("System.IO", "buffer_copy", {TypeKind::Ref, TypeKind::Ref,
                                                            TypeKind::I32},
                             TypeKind::I32, IoBufferCopy));
}

void RegisterSystemBuffer(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.Buffer", "new", {TypeKind::I32}, TypeKind::Ref, BufferNew));
  registry.Register(MakeSpec("System.Buffer", "len", {TypeKind::Ref}, TypeKind::I32, BufferLen));
  registry.Register(MakeSpec("System.Buffer", "readU16LE", {TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, BufferReadU16LE));
  registry.Register(MakeSpec("System.Buffer", "readU32LE", {TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, BufferReadU32LE));
  registry.Register(MakeSpec("System.Buffer", "writeU16LE",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             BufferWriteU16LE));
  registry.Register(MakeSpec("System.Buffer", "writeU32LE",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             BufferWriteU32LE));
  registry.Register(MakeSpec("System.Buffer", "slice",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::Ref,
                             BufferSlice));
  registry.Register(MakeSpec("System.Buffer", "copy",
                             {TypeKind::Ref, TypeKind::I32, TypeKind::Ref, TypeKind::I32,
                              TypeKind::I32},
                             TypeKind::I32, BufferCopy));
}

void RegisterSystemChannel(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  registry.Register(MakeSpec("System.Channel", "newI32", {}, TypeKind::I64, ChannelNewI32));
  registry.Register(MakeSpec("System.Channel", "sendI32", {TypeKind::I64, TypeKind::I32},
                             TypeKind::I32, ChannelSendI32));
  registry.Register(MakeSpec("System.Channel", "trySendI32", {TypeKind::I64, TypeKind::I32},
                             TypeKind::I32, ChannelSendI32));
  registry.Register(MakeSpec("System.Channel", "recvI32", {TypeKind::I64}, TypeKind::I32,
                             ChannelRecvI32));
  registry.Register(MakeSpec("System.Channel", "tryRecvI32", {TypeKind::I64}, TypeKind::I32,
                             ChannelTryRecvI32));
  registry.Register(MakeSpec("System.Channel", "pendingI32", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingI32));
  registry.Register(MakeSpec("System.Channel", "newI64", {}, TypeKind::I64, ChannelNewI64));
  registry.Register(MakeSpec("System.Channel", "sendI64", {TypeKind::I64, TypeKind::I64},
                             TypeKind::I32, ChannelSendI64));
  registry.Register(MakeSpec("System.Channel", "trySendI64", {TypeKind::I64, TypeKind::I64},
                             TypeKind::I32, ChannelSendI64));
  registry.Register(MakeSpec("System.Channel", "recvI64", {TypeKind::I64}, TypeKind::I64,
                             ChannelRecvI64));
  registry.Register(MakeSpec("System.Channel", "tryRecvI64", {TypeKind::I64}, TypeKind::I64,
                             ChannelTryRecvI64));
  registry.Register(MakeSpec("System.Channel", "pendingI64", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingI64));
  registry.Register(MakeSpec("System.Channel", "newF32", {}, TypeKind::I64, ChannelNewF32));
  registry.Register(MakeSpec("System.Channel", "sendF32", {TypeKind::I64, TypeKind::F32},
                             TypeKind::I32, ChannelSendF32));
  registry.Register(MakeSpec("System.Channel", "trySendF32", {TypeKind::I64, TypeKind::F32},
                             TypeKind::I32, ChannelSendF32));
  registry.Register(MakeSpec("System.Channel", "recvF32", {TypeKind::I64}, TypeKind::F32,
                             ChannelRecvF32));
  registry.Register(MakeSpec("System.Channel", "tryRecvF32", {TypeKind::I64}, TypeKind::F32,
                             ChannelTryRecvF32));
  registry.Register(MakeSpec("System.Channel", "pendingF32", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingF32));
  registry.Register(MakeSpec("System.Channel", "newF64", {}, TypeKind::I64, ChannelNewF64));
  registry.Register(MakeSpec("System.Channel", "sendF64", {TypeKind::I64, TypeKind::F64},
                             TypeKind::I32, ChannelSendF64));
  registry.Register(MakeSpec("System.Channel", "trySendF64", {TypeKind::I64, TypeKind::F64},
                             TypeKind::I32, ChannelSendF64));
  registry.Register(MakeSpec("System.Channel", "recvF64", {TypeKind::I64}, TypeKind::F64,
                             ChannelRecvF64));
  registry.Register(MakeSpec("System.Channel", "tryRecvF64", {TypeKind::I64}, TypeKind::F64,
                             ChannelTryRecvF64));
  registry.Register(MakeSpec("System.Channel", "pendingF64", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingF64));
  registry.Register(MakeSpec("System.Channel", "newBool", {}, TypeKind::I64, ChannelNewBool));
  registry.Register(MakeSpec("System.Channel", "sendBool", {TypeKind::I64, TypeKind::Bool},
                             TypeKind::I32, ChannelSendBool));
  registry.Register(MakeSpec("System.Channel", "trySendBool", {TypeKind::I64, TypeKind::Bool},
                             TypeKind::I32, ChannelSendBool));
  registry.Register(MakeSpec("System.Channel", "recvBool", {TypeKind::I64}, TypeKind::Bool,
                             ChannelRecvBool));
  registry.Register(MakeSpec("System.Channel", "tryRecvBool", {TypeKind::I64}, TypeKind::Bool,
                             ChannelTryRecvBool));
  registry.Register(MakeSpec("System.Channel", "pendingBool", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingBool));
  registry.Register(MakeSpec("System.Channel", "newString", {}, TypeKind::I64, ChannelNewString));
  registry.Register(MakeSpec("System.Channel", "sendString", {TypeKind::I64, TypeKind::String},
                             TypeKind::I32, ChannelSendString));
  registry.Register(MakeSpec("System.Channel", "trySendString", {TypeKind::I64, TypeKind::String},
                             TypeKind::I32, ChannelTrySendString));
  registry.Register(MakeSpec("System.Channel", "recvString", {TypeKind::I64}, TypeKind::String,
                             ChannelRecvString));
  registry.Register(MakeSpec("System.Channel", "tryRecvString", {TypeKind::I64}, TypeKind::String,
                             ChannelTryRecvString));
  registry.Register(MakeSpec("System.Channel", "pendingString", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingString));
  registry.Register(MakeSpec("System.Channel", "newBytes", {}, TypeKind::I64, ChannelNewBytes));
  registry.Register(MakeSpec("System.Channel", "sendBytes", {TypeKind::I64, TypeKind::Ref},
                             TypeKind::I32, ChannelSendBytes));
  registry.Register(MakeSpec("System.Channel", "trySendBytes", {TypeKind::I64, TypeKind::Ref},
                             TypeKind::I32, ChannelTrySendBytes));
  registry.Register(MakeSpec("System.Channel", "recvBytes", {TypeKind::I64}, TypeKind::Ref,
                             ChannelRecvBytes));
  registry.Register(MakeSpec("System.Channel", "tryRecvBytes", {TypeKind::I64}, TypeKind::Ref,
                             ChannelTryRecvBytes));
  registry.Register(MakeSpec("System.Channel", "pendingBytes", {TypeKind::I64}, TypeKind::I32,
                             ChannelPendingBytes));
  registry.Register(MakeSpec("System.Channel", "close", {TypeKind::I64}, TypeKind::Unspecified,
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
  RegisterSystemDl(registry);
  return registry;
}

} // namespace Simple::VM::Native
