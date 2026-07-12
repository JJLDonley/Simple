#include "native/registry.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include <utility>

#include "native/buffer.h"
#include "native/fs.h"
#include "ffi/dl_runtime.h"
#include "native/json.h"
#include "native/log.h"
#include "native/path.h"

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
                                 Fs::CopyPath(from, to)
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
  HeapObject* obj = NativeArgHeapObject(context, 1);
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
  HeapObject* obj = NativeArgHeapObject(context, 1);
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

bool IsBufferObject(const HeapObject* obj) {
  return obj && (obj->header.kind == ObjectKind::List || obj->header.kind == ObjectKind::Array) &&
         obj->payload.size() >= 4;
}

size_t BufferElementBase(const HeapObject* obj) {
  return obj && obj->header.kind == ObjectKind::List ? 8u : 4u;
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
  HeapObject* obj = NativeArgHeapObject(context, 0);
  result.value = PackI32(static_cast<int32_t>(Buffer::Len(obj)));
  return result;
}

NativeCallResult BufferReadU16LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = NativeArgHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  result.value = PackI32(offset < 0 ? 0 : static_cast<int32_t>(
      Buffer::ReadLE(obj, static_cast<uint32_t>(offset), 2u)));
  return result;
}

NativeCallResult BufferReadU32LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = NativeArgHeapObject(context, 0);
  const int32_t offset = UnpackI32(context.args[1]);
  result.value = PackI32(offset < 0 ? 0 : static_cast<int32_t>(
      Buffer::ReadLE(obj, static_cast<uint32_t>(offset), 4u)));
  return result;
}

NativeCallResult BufferWriteU16LE(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = NativeArgHeapObject(context, 0);
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
  HeapObject* obj = NativeArgHeapObject(context, 0);
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
  HeapObject* obj = NativeArgHeapObject(context, 0);
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
  HeapObject* dst = NativeArgHeapObject(context, 0);
  const int32_t dst_offset = UnpackI32(context.args[1]);
  HeapObject* src = NativeArgHeapObject(context, 2);
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
  HeapObject* obj = NativeArgHeapObject(context, 0);
  result.value = PackI32(IsBufferObject(obj) ? static_cast<int32_t>(ReadU32(obj->payload, 0)) : -1);
  return result;
}

NativeCallResult IoBufferFill(NativeCallContext& context) {
  NativeCallResult result;
  HeapObject* obj = NativeArgHeapObject(context, 0);
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
  HeapObject* dst = NativeArgHeapObject(context, 0);
  HeapObject* src = NativeArgHeapObject(context, 1);
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

void RegisterSystemPath(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Path);
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Separator), {}, TypeKind::String, PathSeparator));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Delimiter), {}, TypeKind::String, PathDelimiter));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::IsAbsolute), {TypeKind::String}, TypeKind::Bool,
                             PathIsAbsolute));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Join), {TypeKind::String, TypeKind::String},
                             TypeKind::String, PathJoin));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Dirname), {TypeKind::String}, TypeKind::String,
                             PathDirname));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Basename), {TypeKind::String}, TypeKind::String,
                             PathBasename));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Ext), {TypeKind::String}, TypeKind::String,
                             PathExt));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Stem), {TypeKind::String}, TypeKind::String,
                             PathStem));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Normalize), {TypeKind::String}, TypeKind::String,
                             PathNormalize));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::Exists), {TypeKind::String},
                                            TypeKind::I32, PathExists),
                                   "filesystem.read"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::IsFile), {TypeKind::String},
                                            TypeKind::I32, PathIsFile),
                                   "filesystem.read"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemPathMember::IsDir), {TypeKind::String},
                                            TypeKind::I32, PathIsDir),
                                   "filesystem.read"));
}

void RegisterSystemFs(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::FS);
  registry.Register(WithDoc(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::ReadText),
                                                               {TypeKind::String},
                                                               TypeKind::String, FsReadText)),
                                            "filesystem.read"),
                            "Read a UTF-8 text file."));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::WriteText),
                                                  {TypeKind::String, TypeKind::String},
                                                  TypeKind::I32, FsWriteText)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::ReadBytes),
                                                  {TypeKind::String}, TypeKind::Ref, FsReadBytes)),
                                   "filesystem.read"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::WriteBytes),
                                                  {TypeKind::String, TypeKind::Ref},
                                                  TypeKind::I32, FsWriteBytes)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::ListDir),
                                                  {TypeKind::String}, TypeKind::Ref, FsListDir)),
                                   "filesystem.read"));
  registry.Register(WithDoc(
      WithResource(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Open),
                                                   {TypeKind::String, TypeKind::I32},
                                                   TypeKind::I32, FsOpen)),
                                  "filesystem.open"),
                   NativeResourceKind::File, NativeResourceAccess::Output),
      "Open a file descriptor handle."));
  registry.Register(WithResource(
      WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Read),
                                       {TypeKind::I32, TypeKind::Ref, TypeKind::I32},
                                       TypeKind::I32, FsRead)),
                     "filesystem.read"),
      NativeResourceKind::File, NativeResourceAccess::Input, 0));
  registry.Register(WithResource(
      WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Write),
                                       {TypeKind::I32, TypeKind::Ref, TypeKind::I32},
                                       TypeKind::I32, FsWrite)),
                     "filesystem.write"),
      NativeResourceKind::File, NativeResourceAccess::Input, 0));
  registry.Register(WithResource(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Close), {TypeKind::I32},
                                         TypeKind::Unspecified, FsClose),
                                 NativeResourceKind::File, NativeResourceAccess::InputOutput, 0));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Cwd), {}, TypeKind::String, FsCwd),
                                   "filesystem.read"));
  registry.Register(WithCapability(
      WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Copy),
                                       {TypeKind::String, TypeKind::String}, TypeKind::I32,
                                       FsCopy)),
                     "filesystem.read"),
      "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Remove), {TypeKind::String},
                                                   TypeKind::I32, FsRemove)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::Mkdir), {TypeKind::String},
                                                   TypeKind::I32, FsMkdir)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::MkdirAll), {TypeKind::String},
                                                   TypeKind::I32, FsMkdirAll)),
                                   "filesystem.write"));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemFSMember::SetCwd), {TypeKind::String},
                                                   TypeKind::I32, FsSetCwd)),
                                   "filesystem.write"));
}

void RegisterSystemIo(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::IO);
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemIOMember::BufferNew), {TypeKind::I32}, TypeKind::Ref,
                             IoBufferNew));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemIOMember::BufferLen), {TypeKind::Ref}, TypeKind::I32,
                             IoBufferLen));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemIOMember::BufferFill), {TypeKind::Ref, TypeKind::I32,
                                                            TypeKind::I32},
                             TypeKind::I32, IoBufferFill));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemIOMember::BufferCopy), {TypeKind::Ref, TypeKind::Ref,
                                                            TypeKind::I32},
                             TypeKind::I32, IoBufferCopy));
}

void RegisterSystemBuffer(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Buffer);
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::New), {TypeKind::I32}, TypeKind::Ref, BufferNew));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::Len), {TypeKind::Ref}, TypeKind::I32, BufferLen));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::ReadU16LE), {TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, BufferReadU16LE));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::ReadU32LE), {TypeKind::Ref, TypeKind::I32},
                             TypeKind::I32, BufferReadU32LE));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::WriteU16LE),
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             BufferWriteU16LE));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::WriteU32LE),
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::I32,
                             BufferWriteU32LE));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::Slice),
                             {TypeKind::Ref, TypeKind::I32, TypeKind::I32}, TypeKind::Ref,
                             BufferSlice));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemBufferMember::Copy),
                             {TypeKind::Ref, TypeKind::I32, TypeKind::Ref, TypeKind::I32,
                              TypeKind::I32},
                             TypeKind::I32, BufferCopy));
}


} // namespace Simple::VM::Native
