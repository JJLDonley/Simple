#ifndef SIMPLE_VM_INTRINSIC_IDS_H
#define SIMPLE_VM_INTRINSIC_IDS_H

#include <cstdint>

namespace simplevm {

constexpr uint32_t kIntrinsicTrap = 0x0000u;
constexpr uint32_t kIntrinsicBreakpoint = 0x0001u;
constexpr uint32_t kIntrinsicLogI32 = 0x0010u;
constexpr uint32_t kIntrinsicLogI64 = 0x0011u;
constexpr uint32_t kIntrinsicLogF32 = 0x0012u;
constexpr uint32_t kIntrinsicLogF64 = 0x0013u;
constexpr uint32_t kIntrinsicLogRef = 0x0014u;
constexpr uint32_t kIntrinsicAbsI32 = 0x0020u;
constexpr uint32_t kIntrinsicAbsI64 = 0x0021u;
constexpr uint32_t kIntrinsicMinI32 = 0x0022u;
constexpr uint32_t kIntrinsicMaxI32 = 0x0023u;
constexpr uint32_t kIntrinsicMinI64 = 0x0024u;
constexpr uint32_t kIntrinsicMaxI64 = 0x0025u;
constexpr uint32_t kIntrinsicMinF32 = 0x0026u;
constexpr uint32_t kIntrinsicMaxF32 = 0x0027u;
constexpr uint32_t kIntrinsicMinF64 = 0x0028u;
constexpr uint32_t kIntrinsicMaxF64 = 0x0029u;
constexpr uint32_t kIntrinsicMonoNs = 0x0030u;
constexpr uint32_t kIntrinsicWallNs = 0x0031u;
constexpr uint32_t kIntrinsicRandU32 = 0x0040u;
constexpr uint32_t kIntrinsicRandU64 = 0x0041u;
constexpr uint32_t kIntrinsicWriteStdout = 0x0050u;
constexpr uint32_t kIntrinsicWriteStderr = 0x0051u;

} // namespace simplevm

#endif // SIMPLE_VM_INTRINSIC_IDS_H
