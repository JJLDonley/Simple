#include "test_utils.h"

#include "runtime/abi.h"

namespace Simple::VM::Tests {
namespace {

bool VmRuntimeAbiMapsPrimitiveTypes() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::AbiClass;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto bool_info = GetPrimitiveAbiTypeInfo(TypeKind::Bool);
  if (bool_info.abi_class != AbiClass::Scalar || bool_info.size != 1 || bool_info.align != 1) {
    return false;
  }
  const auto char_info = GetPrimitiveAbiTypeInfo(TypeKind::Char);
  if (char_info.abi_class != AbiClass::Scalar || char_info.size != 4 || char_info.align != 4) {
    return false;
  }
  const auto i64_info = GetPrimitiveAbiTypeInfo(TypeKind::I64);
  if (i64_info.abi_class != AbiClass::Scalar || i64_info.size != 8 || i64_info.align != 8) {
    return false;
  }
  const auto f64_info = GetPrimitiveAbiTypeInfo(TypeKind::F64);
  if (f64_info.abi_class != AbiClass::Float || f64_info.size != 8 || f64_info.align != 8) {
    return false;
  }
  const auto string_info = GetPrimitiveAbiTypeInfo(TypeKind::String);
  return string_info.abi_class == AbiClass::Ref && string_info.size == 8 &&
         !string_info.external_ffi_callable;
}

bool VmRuntimeAbiAlignsStableDataFields() {
  using Simple::VM::Runtime::AlignAbiOffset;
  using Simple::VM::Runtime::IsSmallAbiAggregate;

  uint32_t offset = 0;
  offset = AlignAbiOffset(offset, 1) + 1;  // bool
  offset = AlignAbiOffset(offset, 4) + 4;  // i32
  offset = AlignAbiOffset(offset, 8) + 8;  // i64
  const uint32_t size = AlignAbiOffset(offset, 8);
  return size == 16 && IsSmallAbiAggregate(size, false) &&
         !IsSmallAbiAggregate(size, true) && !IsSmallAbiAggregate(24, false);
}

const TestCase kVmRuntimeAbiTests[] = {
  {"vm_runtime_abi_maps_primitive_types", VmRuntimeAbiMapsPrimitiveTypes},
  {"vm_runtime_abi_aligns_stable_data_fields", VmRuntimeAbiAlignsStableDataFields},
};

const TestSection kVmRuntimeAbiSections[] = {
  {"vm_runtime_abi", kVmRuntimeAbiTests, sizeof(kVmRuntimeAbiTests) / sizeof(kVmRuntimeAbiTests[0])},
};

} // namespace

const TestSection* GetVmRuntimeAbiSections(size_t* count) {
  if (count) *count = sizeof(kVmRuntimeAbiSections) / sizeof(kVmRuntimeAbiSections[0]);
  return kVmRuntimeAbiSections;
}

} // namespace Simple::VM::Tests
