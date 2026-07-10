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
  uint64_t payload = 0;
  std::string error;
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
  PromiseStatus Fail(AbiPromiseId id, std::string error);
  PromiseStatus Cancel(AbiPromiseId id);
  size_t Size() const { return records_.size(); }

 private:
  struct Slot {
    PromiseRecord record;
    uint32_t generation = 0;
    bool occupied = false;
  };

  PromiseStatus Complete(AbiPromiseId id, PromiseState state, uint64_t payload, std::string error);

  std::vector<Slot> records_;
};

} // namespace Simple::VM::Runtime
