#include "test_utils.h"

#include "runtime/abi.h"

#include <string>

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

bool VmRuntimeAbiValidatesCallableSignatures() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::ValidateAbiCallableSignature;

  std::string error;
  if (!ValidateAbiCallableSignature({TypeKind::I32, TypeKind::String}, TypeKind::I64,
                                    false, &error) || !error.empty()) {
    return false;
  }
  if (ValidateAbiCallableSignature({TypeKind::Void}, TypeKind::I32, false, &error) ||
      error.find("parameter 0") == std::string::npos) {
    return false;
  }
  error.clear();
  if (ValidateAbiCallableSignature({TypeKind::String}, TypeKind::I32, true, &error) ||
      error.find("parameter 0") == std::string::npos) {
    return false;
  }
  error.clear();
  return !ValidateAbiCallableSignature({TypeKind::I32}, TypeKind::Never, false, &error) &&
         error.find("never") != std::string::npos;
}

bool VmRuntimeAbiComputesStableAggregateLayout() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::ComputeStableAggregateLayout;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto layout = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::Bool),
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::I64),
  });
  if (layout.fields.size() != 3 || layout.fields[0].offset != 0 ||
      layout.fields[1].offset != 4 || layout.fields[2].offset != 8) {
    return false;
  }
  if (layout.size != 16 || layout.align != 8 || !layout.pass_by_value) return false;
  if (!layout.native_callable || !layout.external_ffi_callable || layout.contains_references) {
    return false;
  }

  const auto ref_layout = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::String),
  });
  return ref_layout.fields.size() == 2 && ref_layout.fields[1].offset == 8 &&
         ref_layout.size == 16 && ref_layout.contains_references &&
         !ref_layout.pass_by_value && !ref_layout.external_ffi_callable;
}

const TestCase kVmRuntimeAbiTests[] = {
  {"vm_runtime_abi_maps_primitive_types", VmRuntimeAbiMapsPrimitiveTypes},
  {"vm_runtime_abi_aligns_stable_data_fields", VmRuntimeAbiAlignsStableDataFields},
  {"vm_runtime_abi_validates_callable_signatures", VmRuntimeAbiValidatesCallableSignatures},
  {"vm_runtime_abi_computes_stable_aggregate_layout", VmRuntimeAbiComputesStableAggregateLayout},
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
