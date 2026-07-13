#include "runtime/promise.h"

#include <utility>

namespace Simple::VM::Runtime {
namespace {

constexpr uint32_t kFirstPromiseGeneration = 1;

} // namespace

const char* PromiseStateName(PromiseState state) {
  switch (state) {
    case PromiseState::Pending:
      return "pending";
    case PromiseState::Done:
      return "done";
    case PromiseState::Failed:
      return "failed";
    case PromiseState::Canceled:
      return "canceled";
  }
  return "unknown";
}

const char* PromiseStatusName(PromiseStatus status) {
  switch (status) {
    case PromiseStatus::Ok:
      return "ok";
    case PromiseStatus::InvalidId:
      return "invalid id";
    case PromiseStatus::StaleId:
      return "stale id";
    case PromiseStatus::NotPending:
      return "not pending";
  }
  return "unknown";
}

AbiPromiseId PromiseRegistry::Create() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (uint32_t i = 0; i < records_.size(); ++i) {
    Slot& slot = records_[i];
    if (slot.occupied) continue;
    slot.generation = slot.generation == 0 ? kFirstPromiseGeneration : slot.generation + 1u;
    if (slot.generation == 0) slot.generation = kFirstPromiseGeneration;
    slot.record = PromiseRecord{AbiPromiseId{i, slot.generation}, PromiseState::Pending,
                                false, false, 0, {}, {}};
    slot.occupied = true;
    return slot.record.id;
  }

  Slot slot;
  slot.generation = kFirstPromiseGeneration;
  slot.record = PromiseRecord{AbiPromiseId{static_cast<uint32_t>(records_.size()), slot.generation},
                              PromiseState::Pending, false, false, 0, {}, {}};
  slot.occupied = true;
  records_.push_back(std::move(slot));
  return records_.back().record.id;
}

PromiseStatus PromiseRegistry::Get(AbiPromiseId id, PromiseRecord* out) const {
  if (out) *out = {};
  std::lock_guard<std::mutex> lock(mutex_);
  const Slot* slot = nullptr;
  const PromiseStatus status = ValidateLocked(id, &slot);
  if (status == PromiseStatus::Ok && out) *out = slot->record;
  return status;
}

PromiseStatus PromiseRegistry::Wait(AbiPromiseId id, PromiseRecord* out) {
  if (out) *out = {};
  std::unique_lock<std::mutex> lock(mutex_);
  PromiseStatus status = PromiseStatus::Ok;
  state_changed_.wait(lock, [&] {
    Slot* slot = nullptr;
    status = ValidateLocked(id, &slot);
    return status != PromiseStatus::Ok || slot->record.state != PromiseState::Pending;
  });
  if (status != PromiseStatus::Ok) return status;
  Slot* slot = nullptr;
  status = ValidateLocked(id, &slot);
  if (status == PromiseStatus::Ok && out) *out = slot->record;
  return status;
}

PromiseStatus PromiseRegistry::Resolve(AbiPromiseId id, uint64_t payload) {
  return Complete(id, PromiseState::Done, false, payload, {});
}

PromiseStatus PromiseRegistry::ResolveRef(AbiPromiseId id, uint32_t ref) {
  return Complete(id, PromiseState::Done, true, ref, {});
}

PromiseStatus PromiseRegistry::Fail(AbiPromiseId id, std::string error) {
  return Complete(id, PromiseState::Failed, false, 0, std::move(error));
}

PromiseStatus PromiseRegistry::RequestCancel(AbiPromiseId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  Slot* slot = nullptr;
  const PromiseStatus status = ValidateLocked(id, &slot);
  if (status != PromiseStatus::Ok) return status;
  if (slot->record.state != PromiseState::Pending) return PromiseStatus::NotPending;
  slot->record.cancellation_requested = true;
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::Cancel(AbiPromiseId id) {
  return Complete(id, PromiseState::Canceled, false, 0, {});
}

PromiseStatus PromiseRegistry::Release(AbiPromiseId id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = nullptr;
    const PromiseStatus status = ValidateLocked(id, &slot);
    if (status != PromiseStatus::Ok) return status;
    if (slot->record.state == PromiseState::Pending) return PromiseStatus::NotPending;
    slot->record = {};
    slot->occupied = false;
  }
  state_changed_.notify_all();
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::AddWaiter(AbiPromiseId id, AbiPromiseId waiter) {
  std::lock_guard<std::mutex> lock(mutex_);
  Slot* slot = nullptr;
  const PromiseStatus status = ValidateLocked(id, &slot);
  if (status != PromiseStatus::Ok) return status;
  if (slot->record.state != PromiseState::Pending) return PromiseStatus::NotPending;
  slot->record.waiters.push_back(waiter);
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::DrainWaiters(AbiPromiseId id,
                                            std::vector<AbiPromiseId>* out_waiters) {
  if (!out_waiters) return PromiseStatus::InvalidId;
  std::lock_guard<std::mutex> lock(mutex_);
  Slot* slot = nullptr;
  const PromiseStatus status = ValidateLocked(id, &slot);
  if (status != PromiseStatus::Ok) return status;
  *out_waiters = std::move(slot->record.waiters);
  slot->record.waiters.clear();
  return PromiseStatus::Ok;
}

std::vector<uint32_t> PromiseRegistry::CollectRootRefs() const {
  std::vector<uint32_t> roots;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Slot& slot : records_) {
    if (!slot.occupied) continue;
    const PromiseRecord& record = slot.record;
    if (record.state == PromiseState::Done && record.payload_is_ref) {
      roots.push_back(static_cast<uint32_t>(record.payload));
    }
  }
  return roots;
}

size_t PromiseRegistry::SlotCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

size_t PromiseRegistry::LiveCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t count = 0;
  for (const Slot& slot : records_) {
    if (slot.occupied) ++count;
  }
  return count;
}

PromiseStatus PromiseRegistry::ValidateLocked(AbiPromiseId id, Slot** out_slot) {
  if (out_slot) *out_slot = nullptr;
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  Slot& slot = records_[id.index];
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (out_slot) *out_slot = &slot;
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::ValidateLocked(AbiPromiseId id, const Slot** out_slot) const {
  if (out_slot) *out_slot = nullptr;
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  const Slot& slot = records_[id.index];
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (out_slot) *out_slot = &slot;
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::Complete(AbiPromiseId id,
                                        PromiseState state,
                                        bool payload_is_ref,
                                        uint64_t payload,
                                        std::string error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = nullptr;
    const PromiseStatus status = ValidateLocked(id, &slot);
    if (status != PromiseStatus::Ok) return status;
    PromiseRecord& record = slot->record;
    if (record.state != PromiseState::Pending) return PromiseStatus::NotPending;
    record.state = state;
    record.cancellation_requested = state == PromiseState::Canceled;
    record.payload_is_ref = payload_is_ref;
    record.payload = payload;
    record.error = std::move(error);
  }
  state_changed_.notify_all();
  return PromiseStatus::Ok;
}

} // namespace Simple::VM::Runtime
