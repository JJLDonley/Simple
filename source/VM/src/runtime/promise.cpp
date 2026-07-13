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
  for (uint32_t i = 0; i < records_.size(); ++i) {
    Slot& slot = records_[i];
    if (slot.occupied && slot.record.state == PromiseState::Pending) continue;
    slot.generation = slot.generation == 0 ? kFirstPromiseGeneration : slot.generation + 1;
    if (slot.generation == 0) slot.generation = kFirstPromiseGeneration;
    slot.record = PromiseRecord{AbiPromiseId{i, slot.generation}, PromiseState::Pending, false, false, 0, {}, {}};
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

PromiseStatus PromiseRegistry::Get(AbiPromiseId id, const PromiseRecord** out) const {
  if (out) *out = nullptr;
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  const Slot& slot = records_[id.index];
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  if (out) *out = &slot.record;
  return PromiseStatus::Ok;
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
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  Slot& slot = records_[id.index];
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  if (slot.record.state != PromiseState::Pending) return PromiseStatus::NotPending;
  slot.record.cancellation_requested = true;
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::Cancel(AbiPromiseId id) {
  PromiseStatus status = RequestCancel(id);
  if (status != PromiseStatus::Ok) return status;
  return Complete(id, PromiseState::Canceled, false, 0, {});
}

PromiseStatus PromiseRegistry::AddWaiter(AbiPromiseId id, AbiPromiseId waiter) {
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  Slot& slot = records_[id.index];
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  if (slot.record.state != PromiseState::Pending) return PromiseStatus::NotPending;
  slot.record.waiters.push_back(waiter);
  return PromiseStatus::Ok;
}

PromiseStatus PromiseRegistry::DrainWaiters(AbiPromiseId id, std::vector<AbiPromiseId>* out_waiters) {
  if (!out_waiters) return PromiseStatus::InvalidId;
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  Slot& slot = records_[id.index];
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  *out_waiters = std::move(slot.record.waiters);
  slot.record.waiters.clear();
  return PromiseStatus::Ok;
}

std::vector<uint32_t> PromiseRegistry::CollectRootRefs() const {
  std::vector<uint32_t> roots;
  for (const Slot& slot : records_) {
    if (!slot.occupied) continue;
    const PromiseRecord& record = slot.record;
    if (record.state == PromiseState::Done && record.payload_is_ref) {
      roots.push_back(static_cast<uint32_t>(record.payload));
    }
  }
  return roots;
}

PromiseStatus PromiseRegistry::Complete(AbiPromiseId id,
                                        PromiseState state,
                                        bool payload_is_ref,
                                        uint64_t payload,
                                        std::string error) {
  PromiseRecord* record = nullptr;
  if (id.IsNull() || id.index >= records_.size()) return PromiseStatus::InvalidId;
  Slot& slot = records_[id.index];
  if (!slot.occupied) return PromiseStatus::InvalidId;
  if (slot.generation != id.generation) return PromiseStatus::StaleId;
  record = &slot.record;
  if (record->state != PromiseState::Pending) return PromiseStatus::NotPending;
  record->state = state;
  record->payload_is_ref = payload_is_ref;
  record->payload = payload;
  record->error = std::move(error);
  return PromiseStatus::Ok;
}

} // namespace Simple::VM::Runtime
