#include "native/resource_registry.h"

#include <utility>

namespace Simple::VM::Native {
namespace {

constexpr uint32_t kFirstGeneration = 1;

} // namespace

uint16_t NativeResourceKindId(NativeResourceKind kind) {
  return static_cast<uint16_t>(kind);
}

bool NativeResourceKindFromId(uint16_t id, NativeResourceKind* out_kind) {
  if (!out_kind) return false;
  switch (static_cast<NativeResourceKind>(id)) {
    case NativeResourceKind::Unknown:
    case NativeResourceKind::File:
    case NativeResourceKind::Directory:
    case NativeResourceKind::Socket:
    case NativeResourceKind::Listener:
    case NativeResourceKind::Process:
    case NativeResourceKind::Thread:
    case NativeResourceKind::Job:
    case NativeResourceKind::Channel:
    case NativeResourceKind::FfiLibrary:
    case NativeResourceKind::FfiSymbol:
    case NativeResourceKind::AsmUnit:
    case NativeResourceKind::AsmObject:
    case NativeResourceKind::AsmSymbol:
    case NativeResourceKind::Buffer:
    case NativeResourceKind::Timer:
    case NativeResourceKind::Watcher:
    case NativeResourceKind::Terminal:
    case NativeResourceKind::JsonValue:
    case NativeResourceKind::Logger:
    case NativeResourceKind::Random:
      *out_kind = static_cast<NativeResourceKind>(id);
      return true;
  }
  return false;
}

bool IsKnownNativeResourceKindId(uint16_t id) {
  NativeResourceKind ignored = NativeResourceKind::Unknown;
  return NativeResourceKindFromId(id, &ignored) && ignored != NativeResourceKind::Unknown;
}

uint64_t PackNativeHandleId(NativeHandleId handle) {
  return (static_cast<uint64_t>(handle.generation) << 32u) | handle.index;
}

NativeHandleId UnpackNativeHandleId(uint64_t value) {
  NativeHandleId handle;
  handle.index = static_cast<uint32_t>(value & 0xffffffffu);
  handle.generation = static_cast<uint32_t>(value >> 32u);
  return handle;
}

const char* NativeResourceStatusName(NativeResourceStatus status) {
  switch (status) {
    case NativeResourceStatus::Ok:
      return "ok";
    case NativeResourceStatus::InvalidHandle:
      return "invalid handle";
    case NativeResourceStatus::StaleHandle:
      return "stale handle";
    case NativeResourceStatus::WrongKind:
      return "wrong resource kind";
    case NativeResourceStatus::AlreadyClosed:
      return "resource already closed";
    case NativeResourceStatus::CloseFailed:
      return "resource close failed";
  }
  return "unknown resource status";
}

NativeResourceRegistry::~NativeResourceRegistry() {
  SweepShutdown();
}

NativeHandleId NativeResourceRegistry::Insert(NativeResourceRecord record) {
  for (uint32_t i = 0; i < records_.size(); ++i) {
    Slot& slot = records_[i];
    if (slot.occupied && !slot.record.closed) continue;
    if (slot.record.finalize && slot.record.payload) {
      slot.record.finalize(slot.record.payload);
    }
    slot.generation = slot.generation == 0 ? kFirstGeneration : slot.generation + 1;
    if (slot.generation == 0) slot.generation = kFirstGeneration;
    record.generation = slot.generation;
    record.closed = false;
    slot.record = std::move(record);
    slot.occupied = true;
    return NativeHandleId{i, slot.generation};
  }

  Slot slot;
  slot.generation = kFirstGeneration;
  record.generation = slot.generation;
  record.closed = false;
  slot.record = std::move(record);
  slot.occupied = true;
  records_.push_back(std::move(slot));
  return NativeHandleId{static_cast<uint32_t>(records_.size() - 1u), kFirstGeneration};
}

NativeResourceStatus NativeResourceRegistry::Get(NativeHandleId handle,
                                                 NativeResourceKind expected_kind,
                                                 NativeResourceRecord** out_record) {
  if (out_record) *out_record = nullptr;
  if (handle.IsNull() || handle.index >= records_.size()) return NativeResourceStatus::InvalidHandle;
  Slot& slot = records_[handle.index];
  if (!slot.occupied) return NativeResourceStatus::InvalidHandle;
  if (slot.generation != handle.generation) return NativeResourceStatus::StaleHandle;
  if (slot.record.closed) return NativeResourceStatus::AlreadyClosed;
  if (expected_kind != NativeResourceKind::Unknown && slot.record.kind != expected_kind) {
    return NativeResourceStatus::WrongKind;
  }
  if (out_record) *out_record = &slot.record;
  return NativeResourceStatus::Ok;
}

NativeResourceStatus NativeResourceRegistry::Close(NativeHandleId handle,
                                                   NativeResourceKind expected_kind,
                                                   std::string* error) {
  NativeResourceRecord* record = nullptr;
  const NativeResourceStatus status = Get(handle, expected_kind, &record);
  if (status != NativeResourceStatus::Ok) return status;
  if (record->owned && record->close && !record->close(record->payload, error)) {
    return NativeResourceStatus::CloseFailed;
  }
  record->closed = true;
  return NativeResourceStatus::Ok;
}

size_t NativeResourceRegistry::SweepShutdown() {
  size_t failed_closes = 0;
  for (Slot& slot : records_) {
    if (!slot.occupied) continue;
    NativeResourceRecord& record = slot.record;
    if (!record.closed && record.owned && record.close) {
      std::string ignored;
      if (!record.close(record.payload, &ignored)) ++failed_closes;
    }
    record.closed = true;
    if (record.finalize && record.payload) {
      record.finalize(record.payload);
      record.payload = nullptr;
    }
  }
  return failed_closes;
}

size_t NativeResourceRegistry::LiveCount() const {
  size_t count = 0;
  for (const Slot& slot : records_) {
    if (slot.occupied && !slot.record.closed) ++count;
  }
  return count;
}

} // namespace Simple::VM::Native
