#ifndef SIMPLE_VM_INTRINSIC_IDS_H
#define SIMPLE_VM_INTRINSIC_IDS_H

#include <cstdint>

namespace Simple::VM {

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
constexpr uint32_t kIntrinsicSqrtF32 = 0x002Au;
constexpr uint32_t kIntrinsicSqrtF64 = 0x002Bu;
constexpr uint32_t kIntrinsicMonoNs = 0x0030u;
constexpr uint32_t kIntrinsicWallNs = 0x0031u;
constexpr uint32_t kIntrinsicRandU32 = 0x0040u;
constexpr uint32_t kIntrinsicRandU64 = 0x0041u;
constexpr uint32_t kIntrinsicWriteStdout = 0x0050u;
constexpr uint32_t kIntrinsicWriteStderr = 0x0051u;
constexpr uint32_t kIntrinsicPrintAny = 0x0060u;
constexpr uint32_t kIntrinsicDlCallI8 = 0x0070u;
constexpr uint32_t kIntrinsicDlCallI16 = 0x0071u;
constexpr uint32_t kIntrinsicDlCallI32 = 0x0072u;
constexpr uint32_t kIntrinsicDlCallI64 = 0x0073u;
constexpr uint32_t kIntrinsicDlCallU8 = 0x0074u;
constexpr uint32_t kIntrinsicDlCallU16 = 0x0075u;
constexpr uint32_t kIntrinsicDlCallU32 = 0x0076u;
constexpr uint32_t kIntrinsicDlCallU64 = 0x0077u;
constexpr uint32_t kIntrinsicDlCallF32 = 0x0078u;
constexpr uint32_t kIntrinsicDlCallF64 = 0x0079u;
constexpr uint32_t kIntrinsicDlCallBool = 0x007Au;
constexpr uint32_t kIntrinsicDlCallChar = 0x007Bu;
constexpr uint32_t kIntrinsicDlCallStr0 = 0x007Cu;

constexpr uint32_t kPrintAnyTagI8 = 1u;
constexpr uint32_t kPrintAnyTagI16 = 2u;
constexpr uint32_t kPrintAnyTagI32 = 3u;
constexpr uint32_t kPrintAnyTagI64 = 4u;
constexpr uint32_t kPrintAnyTagU8 = 5u;
constexpr uint32_t kPrintAnyTagU16 = 6u;
constexpr uint32_t kPrintAnyTagU32 = 7u;
constexpr uint32_t kPrintAnyTagU64 = 8u;
constexpr uint32_t kPrintAnyTagF32 = 9u;
constexpr uint32_t kPrintAnyTagF64 = 10u;
constexpr uint32_t kPrintAnyTagBool = 11u;
constexpr uint32_t kPrintAnyTagChar = 12u;
constexpr uint32_t kPrintAnyTagString = 13u;

} // namespace Simple::VM

#endif // SIMPLE_VM_INTRINSIC_IDS_H
