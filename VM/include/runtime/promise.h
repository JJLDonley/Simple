#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/abi.h"

namespace Simple::VM::Runtime {

enum class PromiseState : uint8_t {
  Pending,
  Done,
  Failed,
  Canceled,
};

struct PromiseRecord {
  AbiPromiseId id;
  PromiseState state = PromiseState::Pending;
  bool cancellation_requested = false;
  bool payload_is_ref = false;
  uint64_t payload = 0;
  std::string error;
  std::vector<AbiPromiseId> waiters;
};

enum class PromiseStatus {
  Ok,
  InvalidId,
  StaleId,
  NotPending,
};

const char* PromiseStateName(PromiseState state);
const char* PromiseStatusName(PromiseStatus status);

class PromiseRegistry {
 public:
  PromiseRegistry() = default;
  PromiseRegistry(const PromiseRegistry&) = delete;
  PromiseRegistry& operator=(const PromiseRegistry&) = delete;

  AbiPromiseId Create();
  PromiseStatus Get(AbiPromiseId id, const PromiseRecord** out) const;
  PromiseStatus Resolve(AbiPromiseId id, uint64_t payload);
  PromiseStatus ResolveRef(AbiPromiseId id, uint32_t ref);
  PromiseStatus Fail(AbiPromiseId id, std::string error);
  PromiseStatus RequestCancel(AbiPromiseId id);
  PromiseStatus Cancel(AbiPromiseId id);
  PromiseStatus AddWaiter(AbiPromiseId id, AbiPromiseId waiter);
  PromiseStatus DrainWaiters(AbiPromiseId id, std::vector<AbiPromiseId>* out_waiters);
  std::vector<uint32_t> CollectRootRefs() const;
  size_t Size() const { return records_.size(); }

 private:
  struct Slot {
    PromiseRecord record;
    uint32_t generation = 0;
    bool occupied = false;
  };

  PromiseStatus Complete(AbiPromiseId id, PromiseState state, bool payload_is_ref,
                         uint64_t payload, std::string error);

  std::vector<Slot> records_;
};

} // namespace Simple::VM::Runtime
