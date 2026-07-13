#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sbc_types.h"

namespace Simple::VM::Native {

inline constexpr uint32_t kNativeHandleIndexBits = 21u;
inline constexpr uint32_t kNativeHandleGenerationBits = 21u;
inline constexpr uint32_t kNativeHandleOwnerBits = 22u;
inline constexpr uint32_t kNativeHandleGenerationShift = kNativeHandleIndexBits;
inline constexpr uint32_t kNativeHandleOwnerShift =
    kNativeHandleIndexBits + kNativeHandleGenerationBits;
inline constexpr uint64_t kNativeHandleIndexMask = (uint64_t{1} << kNativeHandleIndexBits) - 1u;
inline constexpr uint64_t kNativeHandleGenerationMask =
    (uint64_t{1} << kNativeHandleGenerationBits) - 1u;
inline constexpr uint64_t kNativeHandleOwnerMask = (uint64_t{1} << kNativeHandleOwnerBits) - 1u;
inline constexpr uint32_t kFirstNativeHandleGeneration = 1u;
inline constexpr uint32_t kNativeResourceNoParameter = 0xffffffffu;

enum class NativeResourceKind : uint16_t {
  Unknown = 0,
  File,
  Directory,
  Socket,
  Listener,
  Process,
  Thread,
  Job,
  Channel,
  FfiLibrary,
  FfiSymbol,
  AsmUnit,
  AsmObject,
  AsmSymbol,
  Buffer,
  Timer,
  Watcher,
  Terminal,
  JsonValue,
  Logger,
  Random,
};

uint16_t NativeResourceKindId(NativeResourceKind kind);
bool NativeResourceKindFromId(uint16_t id, NativeResourceKind* out_kind);
bool IsKnownNativeResourceKindId(uint16_t id);
bool NativeResourceKindFromOpaqueTypeRow(const Simple::Byte::TypeRow& row,
                                         NativeResourceKind* out_kind);

struct NativeHandleId {
  uint32_t index = 0;
  uint32_t generation = 0;
  uint32_t owner = 0;

  bool IsNull() const { return generation == 0 || owner == 0; }
};

uint64_t PackNativeHandleId(NativeHandleId handle);
NativeHandleId UnpackNativeHandleId(uint64_t value);

using NativeResourceCloseFn = bool (*)(void* payload, std::string* error);

struct NativeResourceRecord {
  NativeResourceKind kind = NativeResourceKind::Unknown;
  uint32_t generation = 0;
  uint32_t owner = 0;
  bool owned = true;
  bool closed = true;
  std::string debug_label;
  // C++17 type-erased ownership. The registry is the sole persistent owner;
  // shared_ptr permits typed construction without manual delete/finalizer code.
  std::shared_ptr<void> payload;
  NativeResourceCloseFn close = nullptr;
};

enum class NativeResourceStatus {
  Ok,
  InvalidHandle,
  StaleHandle,
  WrongOwner,
  WrongKind,
  AlreadyClosed,
  CloseFailed,
};

const char* NativeResourceStatusName(NativeResourceStatus status);

class NativeResourceRegistry {
 public:
  NativeResourceRegistry();
  NativeResourceRegistry(const NativeResourceRegistry&) = delete;
  NativeResourceRegistry& operator=(const NativeResourceRegistry&) = delete;
  NativeResourceRegistry(NativeResourceRegistry&&) = delete;
  NativeResourceRegistry& operator=(NativeResourceRegistry&&) = delete;
  ~NativeResourceRegistry();

  NativeHandleId Insert(NativeResourceRecord record);
  NativeResourceStatus Get(NativeHandleId handle,
                           NativeResourceKind expected_kind,
                           NativeResourceRecord** out_record);
  NativeResourceStatus Close(NativeHandleId handle,
                             NativeResourceKind expected_kind,
                             std::string* error);
  // Closes and finalizes all owned live records. Returns the number of close callbacks
  // that reported failure. Records are still marked closed and finalized so shutdown
  // remains best-effort and non-throwing.
  size_t SweepShutdown();

  size_t LiveCount() const;
  size_t SlotCount() const { return records_.size(); }
  uint32_t OwnerId() const { return owner_id_; }

 private:
  struct Slot {
    NativeResourceRecord record;
    uint32_t generation = 0;
    bool occupied = false;
  };

  uint32_t owner_id_ = 0;
  std::vector<Slot> records_;
};

} // namespace Simple::VM::Native
