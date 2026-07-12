#include "native/registry.h"
#include "native/slot_codec.h"

#include "native/arg_utils.h"
#include "native/buffer.h"
#include "native/spec_builder.h"

#include <cstddef>

namespace Simple::VM::Native {
namespace {

bool IsBufferObject(const HeapObject* obj) {
  return obj && (obj->header.kind == ObjectKind::List || obj->header.kind == ObjectKind::Array) &&
         obj->payload.size() >= 4;
}

size_t BufferElementBase(const HeapObject* obj) {
  return obj && obj->header.kind == ObjectKind::List ? 8u : 4u;
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
