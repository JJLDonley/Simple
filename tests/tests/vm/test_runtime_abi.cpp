#include "test_utils.h"

#include "ffi/dl_call.h"
#include "native/registry.h"
#include "runtime/abi.h"
#include "runtime/promise.h"
#include "runtime/type_identity.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace Simple::VM::Tests {
namespace {

bool VmRuntimeAbiManglesGenericSymbols() {
  using Simple::VM::Runtime::DetectGenericSymbolCollision;
  using Simple::VM::Runtime::HumanGenericSymbolName;
  using Simple::VM::Runtime::LinkGenericSymbolName;

  const std::vector<std::string> args = {"string", "list<i32>"};
  const std::string human = HumanGenericSymbolName("Map", args);
  const std::string link = LinkGenericSymbolName("Map", args);
  const std::string escaped = LinkGenericSymbolName("Map Value", args);
  std::string error;
  if (human != "Map<string, list<i32>>" || link.rfind("Map$g$", 0) != 0 ||
      escaped.rfind("Map%20Value$g$", 0) != 0 || link == escaped) {
    return false;
  }
  if (!DetectGenericSymbolCollision({{link, human}, {link, human}}, &error) || !error.empty()) {
    return false;
  }
  return !DetectGenericSymbolCollision({{link, human}, {link, "Other<i32>"}}, &error) &&
         error.find("generic symbol collision") != std::string::npos;
}

bool VmRuntimeAbiBuildsCanonicalTypeIdentities() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Native::NativeResourceKind;
  using Simple::VM::Runtime::CanonicalAggregateTypeIdentity;
  using Simple::VM::Runtime::CanonicalArrayTypeIdentity;
  using Simple::VM::Runtime::CanonicalBytesTypeIdentity;
  using Simple::VM::Runtime::CanonicalChannelTypeIdentity;
  using Simple::VM::Runtime::CanonicalEnumTypeIdentity;
  using Simple::VM::Runtime::CanonicalHandleTypeIdentity;
  using Simple::VM::Runtime::CanonicalFunctionTypeIdentity;
  using Simple::VM::Runtime::CanonicalInstantiatedTypeIdentity;
  using Simple::VM::Runtime::CanonicalListTypeIdentity;
  using Simple::VM::Runtime::CanonicalOptionalTypeIdentity;
  using Simple::VM::Runtime::CanonicalPointerTypeIdentity;
  using Simple::VM::Runtime::CanonicalPrimitiveTypeIdentity;
  using Simple::VM::Runtime::CanonicalPromiseTypeIdentity;
  using Simple::VM::Runtime::CanonicalResultTypeIdentity;
  using Simple::VM::Runtime::ComputeStableAggregateLayout;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto layout = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::F64),
  });
  const std::string data_id = CanonicalAggregateTypeIdentity(layout);
  return CanonicalPrimitiveTypeIdentity(TypeKind::I32) == "i32" &&
         CanonicalPrimitiveTypeIdentity(TypeKind::String) == "string" &&
         CanonicalEnumTypeIdentity("Color Mode", TypeKind::U8) == "enum:Color%20Mode:u8" &&
         CanonicalPointerTypeIdentity("i32") == "ptr<i32>" &&
         CanonicalBytesTypeIdentity() == "bytes" &&
         CanonicalArrayTypeIdentity("i32") == "array<i32>" &&
         CanonicalListTypeIdentity("string") == "list<string>" &&
         CanonicalFunctionTypeIdentity({"i32", "string"}, "bool", false) ==
             "fn(i32,string)->bool" &&
         CanonicalFunctionTypeIdentity({"i32"}, "void", true) == "closure(i32)->void" &&
         CanonicalHandleTypeIdentity(NativeResourceKind::File) == "handle#1" &&
         CanonicalChannelTypeIdentity("i32") == "channel<i32>" &&
         CanonicalInstantiatedTypeIdentity("Map", {"string", "list<i32>"}) ==
             "inst<Map,string,list<i32>>" &&
         data_id.rfind("data#", 0) == 0 &&
         data_id.find(":" + std::to_string(layout.size) + ":" + std::to_string(layout.align)) !=
             std::string::npos &&
         CanonicalOptionalTypeIdentity("i32") == "optional<i32>" &&
         CanonicalResultTypeIdentity("i32", "string") == "result<i32,string>" &&
         CanonicalPromiseTypeIdentity("string") == "promise<string>";
}

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

bool VmRuntimeAbiValidatesScalarValues() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::IsValidAbiScalarValue;

  return IsValidAbiScalarValue(TypeKind::Bool, 0) &&
         IsValidAbiScalarValue(TypeKind::Bool, 1) &&
         !IsValidAbiScalarValue(TypeKind::Bool, 2) &&
         IsValidAbiScalarValue(TypeKind::Char, 0x41) &&
         IsValidAbiScalarValue(TypeKind::Char, 0x10FFFF) &&
         !IsValidAbiScalarValue(TypeKind::Char, 0xD800) &&
         !IsValidAbiScalarValue(TypeKind::Char, 0x110000) &&
         IsValidAbiScalarValue(TypeKind::I32, 0xFFFFFFFFu) &&
         !IsValidAbiScalarValue(TypeKind::String, 1);
}

bool VmRuntimeAbiMapsOpaqueHandleTypeRows() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::AbiClass;
  using Simple::VM::Runtime::GetAbiParameterPassMode;
  using Simple::VM::Runtime::GetSbcTypeAbiTypeInfo;
  using Simple::VM::Runtime::AbiPassMode;

  Simple::Byte::TypeRow handle;
  handle.kind = static_cast<uint8_t>(TypeKind::Unspecified);
  handle.flags = Simple::Byte::kTypeFlagOpaqueHandle;
  handle.reserved = 1;
  handle.size = 8;
  const auto handle_info = GetSbcTypeAbiTypeInfo(handle);

  Simple::Byte::TypeRow scalar;
  scalar.kind = static_cast<uint8_t>(TypeKind::I64);
  scalar.size = 8;
  const auto scalar_info = GetSbcTypeAbiTypeInfo(scalar);

  Simple::Byte::TypeRow tagged;
  tagged.kind = static_cast<uint8_t>(TypeKind::Result);
  tagged.flags = Simple::Byte::kTypeFlagManagedArtifact;
  tagged.size = 12;
  const auto tagged_info = GetSbcTypeAbiTypeInfo(tagged);

  return handle_info.abi_class == AbiClass::Handle && handle_info.size == 8 &&
         handle_info.align == 8 && handle_info.native_callable &&
         !handle_info.external_ffi_callable &&
         GetAbiParameterPassMode(handle_info) == AbiPassMode::Direct &&
         scalar_info.abi_class == AbiClass::Scalar && scalar_info.size == 8 &&
         tagged_info.abi_class == AbiClass::Ref && tagged_info.size == 8 &&
         tagged_info.native_callable && !tagged_info.external_ffi_callable;
}

bool VmRuntimeAbiMapsStableSbcDataTypes() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::AbiClass;
  using Simple::VM::Runtime::GetSbcModuleTypeAbiTypeInfo;

  Simple::Byte::SbcModule module;
  Simple::Byte::TypeRow i32;
  i32.kind = static_cast<uint8_t>(TypeKind::I32);
  i32.size = 4;
  module.types.push_back(i32);
  Simple::Byte::TypeRow data;
  data.kind = static_cast<uint8_t>(TypeKind::Unspecified);
  data.flags = Simple::Byte::kTypeFlagStableData;
  data.size = 8;
  data.field_start = 0;
  data.field_count = 2;
  module.types.push_back(data);
  module.fields.push_back(Simple::Byte::FieldRow{0, 0, 0, 0});
  module.fields.push_back(Simple::Byte::FieldRow{0, 0, 4, 0});

  Simple::VM::Runtime::AbiTypeInfo info;
  std::string error;
  if (!GetSbcModuleTypeAbiTypeInfo(module, 1, &info, &error) || !error.empty()) {
    return false;
  }
  if (info.abi_class != AbiClass::Aggregate || info.size != 8 || info.align != 4 ||
      !info.native_callable || !info.external_ffi_callable) {
    return false;
  }

  module.fields[1].type_id = 1;
  error.clear();
  return !GetSbcModuleTypeAbiTypeInfo(module, 1, &info, &error) &&
         error.find("recursive stable data ABI type") != std::string::npos;
}

bool VmRuntimeAbiMapsEnumUnderlyingTypes() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::AbiClass;
  using Simple::VM::Runtime::GetEnumAbiTypeInfo;

  const auto default_enum = GetEnumAbiTypeInfo(TypeKind::Unspecified);
  const auto small_enum = GetEnumAbiTypeInfo(TypeKind::U8);
  const auto wide_enum = GetEnumAbiTypeInfo(TypeKind::I64);
  const auto bad_enum = GetEnumAbiTypeInfo(TypeKind::String);
  return default_enum.abi_class == AbiClass::Scalar && default_enum.size == 4 &&
         default_enum.align == 4 && small_enum.abi_class == AbiClass::Scalar &&
         small_enum.size == 1 && small_enum.align == 1 &&
         wide_enum.abi_class == AbiClass::Scalar && wide_enum.size == 8 &&
         wide_enum.align == 8 && bad_enum.abi_class == AbiClass::Invalid;
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

bool VmRuntimePromiseRegistryTracksStates() {
  using Simple::VM::Runtime::PromiseRegistry;
  using Simple::VM::Runtime::PromiseState;
  using Simple::VM::Runtime::PromiseStatus;
  using Simple::VM::Runtime::PromiseStatusName;

  PromiseRegistry registry;
  const auto first = registry.Create();
  Simple::VM::Runtime::PromiseRecord record;
  if (registry.Get(first, &record) != PromiseStatus::Ok ||
      record.state != PromiseState::Pending) {
    return false;
  }
  const auto waiter = registry.Create();
  if (registry.AddWaiter(first, waiter) != PromiseStatus::Ok) return false;
  if (registry.RequestCancel(first) != PromiseStatus::Ok) return false;
  if (registry.Get(first, &record) != PromiseStatus::Ok || !record.cancellation_requested ||
      record.waiters.size() != 1) {
    return false;
  }
  std::vector<Simple::VM::Runtime::AbiPromiseId> drained;
  if (registry.DrainWaiters(first, &drained) != PromiseStatus::Ok || drained.size() != 1 ||
      drained[0].index != waiter.index) {
    return false;
  }
  if (registry.Resolve(first, 99) != PromiseStatus::Ok) return false;
  if (registry.Resolve(first, 100) != PromiseStatus::NotPending) return false;
  if (registry.Get(first, &record) != PromiseStatus::Ok || record.state != PromiseState::Done ||
      record.payload != 99 || !record.waiters.empty()) {
    return false;
  }
  if (registry.Release(first) != PromiseStatus::Ok) return false;

  const auto reused = registry.Create();
  if (reused.index != first.index || reused.generation == first.generation) return false;
  if (registry.Get(first, nullptr) != PromiseStatus::StaleId) return false;
  if (registry.Fail(reused, "boom") != PromiseStatus::Ok) return false;
  if (registry.Get(reused, &record) != PromiseStatus::Ok || record.state != PromiseState::Failed ||
      record.error != "boom") {
    return false;
  }

  const auto rooted = registry.Create();
  if (registry.ResolveRef(rooted, 1234) != PromiseStatus::Ok) return false;
  const std::vector<uint32_t> roots = registry.CollectRootRefs();
  if (roots.size() != 1 || roots[0] != 1234) return false;

  const auto canceled = registry.Create();
  if (registry.Cancel(canceled) != PromiseStatus::Ok) return false;
  if (registry.Get(canceled, &record) != PromiseStatus::Ok ||
      record.state != PromiseState::Canceled || !record.cancellation_requested) {
    return false;
  }
  return PromiseStatusName(PromiseStatus::NotPending) == std::string("not pending");
}

bool VmRuntimePromiseRegistryWaitsAcrossThreads() {
  using Simple::VM::Runtime::PromiseRecord;
  using Simple::VM::Runtime::PromiseRegistry;
  using Simple::VM::Runtime::PromiseState;
  using Simple::VM::Runtime::PromiseStatus;

  PromiseRegistry registry;
  const auto promise = registry.Create();
  std::thread worker([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    (void)registry.Resolve(promise, 77);
  });
  PromiseRecord record;
  const PromiseStatus wait_status = registry.Wait(promise, &record);
  worker.join();
  if (wait_status != PromiseStatus::Ok || record.state != PromiseState::Done ||
      record.payload != 77 || registry.LiveCount() != 1) {
    return false;
  }
  if (registry.Release(promise) != PromiseStatus::Ok || registry.LiveCount() != 0) {
    return false;
  }
  const auto reused = registry.Create();
  return reused.index == promise.index && reused.generation != promise.generation &&
         registry.Get(promise, nullptr) == PromiseStatus::StaleId;
}

bool VmRuntimeAbiPacksPromiseIds() {
  using Simple::VM::Runtime::AbiPromiseId;
  using Simple::VM::Runtime::PackAbiPromiseId;
  using Simple::VM::Runtime::UnpackAbiPromiseId;

  const AbiPromiseId promise{123, 45};
  const uint64_t packed = PackAbiPromiseId(promise);
  const AbiPromiseId unpacked = UnpackAbiPromiseId(packed);
  const AbiPromiseId null_promise{};
  return packed == ((45ull << 32u) | 123ull) && unpacked.index == promise.index &&
         unpacked.generation == promise.generation && null_promise.IsNull() &&
         !promise.IsNull();
}

bool VmRuntimeAbiBuildsResultAndOptionalValues() {
  using Simple::VM::Runtime::AbiVariantTag;
  using Simple::VM::Runtime::AbiVariantValue;
  using Simple::VM::Runtime::IsAbiOptionalPresent;
  using Simple::VM::Runtime::IsAbiResultError;
  using Simple::VM::Runtime::IsAbiResultValue;
  using Simple::VM::Runtime::MakeAbiOptionalAbsent;
  using Simple::VM::Runtime::MakeAbiOptionalPresent;
  using Simple::VM::Runtime::MakeAbiResultError;
  using Simple::VM::Runtime::MakeAbiResultValue;

  static_assert(sizeof(AbiVariantValue) == 16, "ABI variants remain 16 bytes");
  const AbiVariantValue zero_initialized{};
  const AbiVariantValue absent = MakeAbiOptionalAbsent();
  const AbiVariantValue present = MakeAbiOptionalPresent(42);
  const AbiVariantValue value = MakeAbiResultValue(7);
  const AbiVariantValue error_value = MakeAbiResultError(9);
  return static_cast<uint32_t>(AbiVariantTag::Value) == 0 &&
         static_cast<uint32_t>(AbiVariantTag::Error) == 1 &&
         IsAbiResultValue(zero_initialized) && !IsAbiResultError(zero_initialized) &&
         absent.tag == AbiVariantTag::Absent && absent.payload == 0 &&
         !IsAbiOptionalPresent(absent) && present.tag == AbiVariantTag::Present &&
         present.payload == 42 && IsAbiOptionalPresent(present) &&
         value.tag == AbiVariantTag::Value && value.payload == 7 &&
         IsAbiResultValue(value) && error_value.tag == AbiVariantTag::Error &&
         error_value.payload == 9 && IsAbiResultError(error_value) &&
         !IsAbiResultError(value);
}

bool VmRuntimeAbiClassifiesPassModes() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::AbiPassMode;
  using Simple::VM::Runtime::ComputeStableAggregateLayout;
  using Simple::VM::Runtime::GetAbiAggregatePassMode;
  using Simple::VM::Runtime::GetAbiParameterPassMode;
  using Simple::VM::Runtime::GetAbiReturnPassMode;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto i32 = GetPrimitiveAbiTypeInfo(TypeKind::I32);
  const auto string_ref = GetPrimitiveAbiTypeInfo(TypeKind::String);
  const auto none = GetPrimitiveAbiTypeInfo(TypeKind::Unspecified);
  const auto small = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
  });
  const auto with_ref = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::String),
  });
  return GetAbiParameterPassMode(i32) == AbiPassMode::Direct &&
         GetAbiReturnPassMode(i32) == AbiPassMode::Direct &&
         GetAbiParameterPassMode(string_ref) == AbiPassMode::Direct &&
         GetAbiReturnPassMode(none) == AbiPassMode::Void &&
         GetAbiAggregatePassMode(small) == AbiPassMode::Direct &&
         GetAbiAggregatePassMode(with_ref) == AbiPassMode::Indirect;
}

bool VmRuntimeAbiValidatesBorrowedViews() {
  using Simple::VM::Runtime::AbiStringEncoding;
  using Simple::VM::Runtime::IsValidBorrowedBytesView;
  using Simple::VM::Runtime::IsValidBorrowedStringView;
  using Simple::VM::Runtime::SimpleBytesView;
  using Simple::VM::Runtime::SimpleStringView;

  const char text[] = "abc";
  const uint8_t bytes[] = {1, 2, 3};
  const SimpleStringView good_string{text, 3, AbiStringEncoding::Utf8};
  const SimpleStringView empty_string{nullptr, 0, AbiStringEncoding::Utf8};
  const SimpleStringView bad_string{nullptr, 3, AbiStringEncoding::Utf8};
  const SimpleBytesView good_bytes{bytes, 3};
  const SimpleBytesView empty_bytes{nullptr, 0};
  const SimpleBytesView bad_bytes{nullptr, 3};
  return IsValidBorrowedStringView(good_string) && IsValidBorrowedStringView(empty_string) &&
         !IsValidBorrowedStringView(bad_string) && IsValidBorrowedBytesView(good_bytes) &&
         IsValidBorrowedBytesView(empty_bytes) && !IsValidBorrowedBytesView(bad_bytes);
}

bool VmRuntimeAbiMarksOpaqueVmReferences() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::IsOpaqueVmReferenceType;

  return IsOpaqueVmReferenceType(TypeKind::Ref) &&
         IsOpaqueVmReferenceType(TypeKind::String) &&
         IsOpaqueVmReferenceType(TypeKind::Array) &&
         IsOpaqueVmReferenceType(TypeKind::List) &&
         IsOpaqueVmReferenceType(TypeKind::Function) &&
         !IsOpaqueVmReferenceType(TypeKind::Ptr) &&
         !IsOpaqueVmReferenceType(TypeKind::I64);
}

bool VmRuntimeAbiAcceptsExplicitExternalWrappers() {
  using Simple::VM::Runtime::AbiClass;
  using Simple::VM::Runtime::AbiExternalWrapperKind;
  using Simple::VM::Runtime::AbiPassMode;
  using Simple::VM::Runtime::GetAbiParameterPassMode;
  using Simple::VM::Runtime::GetExternalCAbiWrapperTypeInfo;
  using Simple::VM::Runtime::ValidateExternalCAbiTypeInfos;

  const auto c_string = GetExternalCAbiWrapperTypeInfo(AbiExternalWrapperKind::CString);
  const auto string_view = GetExternalCAbiWrapperTypeInfo(AbiExternalWrapperKind::StringView);
  const auto bytes_view = GetExternalCAbiWrapperTypeInfo(AbiExternalWrapperKind::BytesView);
  std::string error;
  return c_string.abi_class == AbiClass::Scalar && c_string.size == 8 && c_string.align == 8 &&
         GetAbiParameterPassMode(c_string) == AbiPassMode::Direct &&
         string_view.abi_class == AbiClass::Aggregate && string_view.size == 16 &&
         string_view.align == 8 && bytes_view.abi_class == AbiClass::Aggregate &&
         bytes_view.size == 16 && bytes_view.align == 8 &&
         ValidateExternalCAbiTypeInfos({c_string, string_view, bytes_view}, c_string, &error) &&
         error.empty();
}

bool VmRuntimeAbiValidatesExternalCSignatures() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::ComputeStableAggregateLayout;
  using Simple::VM::Runtime::GetAggregateAbiTypeInfo;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;
  using Simple::VM::Runtime::ValidateExternalCAbiSignature;
  using Simple::VM::Runtime::ValidateExternalCAbiTypeInfos;

  std::string error;
  if (!ValidateExternalCAbiSignature({TypeKind::I32, TypeKind::Ptr}, TypeKind::I64, &error) ||
      !error.empty()) {
    return false;
  }
  const auto stable_data = GetAggregateAbiTypeInfo(ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::F64),
  }));
  if (!ValidateExternalCAbiTypeInfos({stable_data}, stable_data, &error) || !error.empty()) {
    return false;
  }
  const auto managed_data = GetAggregateAbiTypeInfo(ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::String),
  }));
  if (ValidateExternalCAbiTypeInfos({managed_data}, stable_data, &error) ||
      error.find("parameter 0") == std::string::npos) {
    return false;
  }
  error.clear();
  if (ValidateExternalCAbiSignature({TypeKind::String}, TypeKind::I32, &error) ||
      error.find("string") == std::string::npos) {
    return false;
  }
  error.clear();
  if (ValidateExternalCAbiSignature({TypeKind::Ref}, TypeKind::I32, &error) ||
      error.find("ref") == std::string::npos) {
    return false;
  }
  error.clear();
  if (ValidateExternalCAbiSignature({TypeKind::I32}, TypeKind::Result, &error) ||
      error.find("result") == std::string::npos) {
    return false;
  }
  error.clear();
  return !ValidateExternalCAbiSignature({TypeKind::I32}, TypeKind::Optional, &error) &&
         error.find("optional") != std::string::npos;
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

bool VmRuntimeAbiComputesStableLayoutHashes() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::ComputeStableAggregateLayout;
  using Simple::VM::Runtime::ComputeStableAggregateLayoutHash;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto first = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::Bool),
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
  });
  const auto same = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::Bool),
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
  });
  const auto reordered = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::Bool),
  });
  return first.layout_hash != 0 && first.layout_hash == ComputeStableAggregateLayoutHash(first) &&
         first.layout_hash == same.layout_hash && first.layout_hash != reordered.layout_hash;
}

bool VmRuntimeAbiComputesNestedAggregateLayout() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::ComputeStableAggregateLayout;
  using Simple::VM::Runtime::GetAggregateAbiTypeInfo;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto inner = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I64),
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
  });
  const auto outer = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I8),
      GetAggregateAbiTypeInfo(inner),
      GetPrimitiveAbiTypeInfo(TypeKind::I16),
  });
  return inner.size == 16 && inner.align == 8 &&
         outer.fields.size() == 3 && outer.fields[0].offset == 0 &&
         outer.fields[1].offset == 8 && outer.fields[2].offset == 24 &&
         outer.align == 8 && outer.size == 32 && !outer.pass_by_value &&
         outer.padding.size() == 2 && outer.padding[0].offset == 1 &&
         outer.padding[0].size == 7 && outer.padding[1].offset == 26 &&
         outer.padding[1].size == 6;
}

bool VmRuntimeAbiComputesFixedArrayLayout() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::ComputeStableFixedArrayLayout;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  const auto i16_array = ComputeStableFixedArrayLayout(GetPrimitiveAbiTypeInfo(TypeKind::I16), 3);
  const auto i32_array = ComputeStableFixedArrayLayout(GetPrimitiveAbiTypeInfo(TypeKind::I32), 4);
  const auto ref_array = ComputeStableFixedArrayLayout(GetPrimitiveAbiTypeInfo(TypeKind::String), 2);
  return i16_array.element_stride == 2 && i16_array.size == 6 && i16_array.align == 2 &&
         i16_array.pass_by_value && i16_array.external_ffi_callable &&
         i32_array.element_stride == 4 && i32_array.size == 16 && i32_array.align == 4 &&
         i32_array.pass_by_value && ref_array.element_stride == 8 && ref_array.size == 16 &&
         ref_array.contains_references && !ref_array.pass_by_value &&
         !ref_array.external_ffi_callable;
}

bool VmRuntimeAbiDataMethodsDoNotAffectLayout() {
  using Simple::Byte::TypeKind;
  using Simple::VM::Runtime::AbiDataDeclaration;
  using Simple::VM::Runtime::ComputeStableDataLayout;
  using Simple::VM::Runtime::GetPrimitiveAbiTypeInfo;

  AbiDataDeclaration without_methods;
  without_methods.fields = {
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::F64),
  };

  AbiDataDeclaration with_methods = without_methods;
  with_methods.method_count = 7;

  const auto first = ComputeStableDataLayout(without_methods);
  const auto second = ComputeStableDataLayout(with_methods);
  return first.size == second.size && first.align == second.align &&
         first.layout_hash == second.layout_hash && first.fields.size() == second.fields.size() &&
         first.fields[0].offset == second.fields[0].offset &&
         first.fields[1].offset == second.fields[1].offset;
}

bool VmRuntimeAbiRejectsRecursiveValueContainment() {
  using Simple::VM::Runtime::AbiContainmentField;
  using Simple::VM::Runtime::ValidateNoRecursiveValueContainment;

  std::string error;
  const std::vector<std::vector<AbiContainmentField>> direct_cycle = {
      {AbiContainmentField{1, false}},
      {AbiContainmentField{0, false}},
  };
  if (ValidateNoRecursiveValueContainment(direct_cycle, &error) ||
      error.find("recursive value containment") == std::string::npos) {
    return false;
  }

  error.clear();
  const std::vector<std::vector<AbiContainmentField>> indirect_cycle = {
      {AbiContainmentField{1, false}},
      {AbiContainmentField{0, true}},
  };
  if (!ValidateNoRecursiveValueContainment(indirect_cycle, &error) || !error.empty()) {
    return false;
  }

  const std::vector<std::vector<AbiContainmentField>> invalid_field = {
      {AbiContainmentField{3, false}},
  };
  return !ValidateNoRecursiveValueContainment(invalid_field, &error) &&
         error.find("out of range") != std::string::npos;
}

bool VmRuntimeAbiValidatesDynamicDlAbi() {
  using Simple::Byte::TypeKind;

  Simple::Byte::SbcModule module;
  Simple::Byte::TypeRow i32;
  i32.kind = static_cast<uint8_t>(TypeKind::I32);
  i32.size = 4;
  module.types.push_back(i32);
  Simple::Byte::TypeRow i64;
  i64.kind = static_cast<uint8_t>(TypeKind::I64);
  i64.size = 8;
  module.types.push_back(i64);
  Simple::Byte::TypeRow string;
  string.kind = static_cast<uint8_t>(TypeKind::String);
  string.size = 8;
  module.types.push_back(string);
  Simple::Byte::TypeRow ref;
  ref.kind = static_cast<uint8_t>(TypeKind::Ref);
  ref.size = 8;
  module.types.push_back(ref);
  Simple::Byte::TypeRow ptr;
  ptr.kind = static_cast<uint8_t>(TypeKind::Ptr);
  ptr.size = 8;
  module.types.push_back(ptr);
  Simple::Byte::TypeRow point;
  point.kind = static_cast<uint8_t>(TypeKind::Unspecified);
  point.size = 8;
  point.field_start = 0;
  point.field_count = 2;
  module.types.push_back(point);
  module.fields.push_back(Simple::Byte::FieldRow{0, 0, 0, 0});
  module.fields.push_back(Simple::Byte::FieldRow{0, 0, 4, 0});

  auto scalar = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module, 0, true, {4, 0});
  if (!scalar.abi_valid || !scalar.vm_marshal_supported || !scalar.jit_helper_safe || !scalar.jit_loop_safe ||
      scalar.may_allocate || scalar.needs_roots) {
    return false;
  }

  auto cstring = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module, 0, true, {4, 2});
  if (cstring.abi_valid || cstring.vm_marshal_supported ||
      cstring.reason.find("unsupported VM marshal type") == std::string::npos) {
    return false;
  }

  auto aggregate = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module, 0, false, {4, 5});
  if (!aggregate.abi_valid || !aggregate.vm_marshal_supported || !aggregate.jit_helper_safe ||
      aggregate.jit_loop_safe || !aggregate.needs_roots || aggregate.may_allocate) {
    return false;
  }

  auto aggregate_ret = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module, 5, true, {4});
  if (!aggregate_ret.abi_valid || !aggregate_ret.vm_marshal_supported || !aggregate_ret.jit_helper_safe ||
      aggregate_ret.jit_loop_safe || !aggregate_ret.needs_roots || !aggregate_ret.may_allocate) {
    return false;
  }

  auto bad_ptr = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module, 0, true, {0, 0});
  if (bad_ptr.abi_valid || bad_ptr.vm_marshal_supported || bad_ptr.reason.find("function pointer") == std::string::npos) {
    return false;
  }

  auto bad_ref_param = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module, 0, true, {4, 3});
  return !bad_ref_param.abi_valid && !bad_ref_param.vm_marshal_supported &&
         bad_ref_param.reason.find("unsupported VM marshal type") != std::string::npos;
}

bool VmRuntimeAbiClassifiesNativeJitCalls() {
  using Simple::Byte::TypeKind;
  using namespace Simple::VM::Native;

  NativeFunctionSpec spec;
  spec.module_name = "Test";
  spec.symbol_name = "pure";
  spec.parameter_types = {TypeKind::String, TypeKind::I32};
  spec.result_type = TypeKind::I32;
  spec.doc_summary = "pure native JIT classifier test";
  spec.blocking = NativeBlockingBehavior::NonBlocking;
  spec.allocation = NativeAllocationBehavior::NoAllocation;
  spec.gc_behavior = NativeGcBehavior::NoSafepoint;
  spec.handler = [](NativeCallContext&) { return NativeCallResult::I32(0); };

  auto pure = AnalyzeNativeJitCall(spec, {TypeKind::String, TypeKind::I32}, TypeKind::I32);
  if (!pure.metadata_valid || !pure.signature_matches || !pure.jit_helper_safe || !pure.jit_loop_safe ||
      !pure.needs_roots || pure.may_allocate || pure.may_block) {
    return false;
  }

  NativeFunctionSpec missing_handler = spec;
  missing_handler.handler = nullptr;
  auto invalid = AnalyzeNativeJitCall(missing_handler, {TypeKind::String, TypeKind::I32}, TypeKind::I32);
  if (invalid.metadata_valid || invalid.jit_helper_safe || invalid.reason != "invalid-native-abi-metadata") return false;

  auto mismatch = AnalyzeNativeJitCall(spec, {TypeKind::I32}, TypeKind::I32);
  if (mismatch.jit_loop_safe || mismatch.reason != "metadata-signature-mismatch") return false;

  spec.blocking = NativeBlockingBehavior::MayBlock;
  auto blocking = AnalyzeNativeJitCall(spec, {TypeKind::String, TypeKind::I32}, TypeKind::I32);
  if (blocking.jit_loop_safe || blocking.reason != "blocking-call") return false;
  spec.blocking = NativeBlockingBehavior::NonBlocking;

  spec.allocation = NativeAllocationBehavior::MayAllocateVm;
  spec.gc_behavior = NativeGcBehavior::MaySafepoint;
  auto allocating = AnalyzeNativeJitCall(spec, {TypeKind::String, TypeKind::I32}, TypeKind::I32);
  if (allocating.jit_loop_safe || allocating.reason != "allocating-call") return false;
  spec.allocation = NativeAllocationBehavior::NoAllocation;
  spec.gc_behavior = NativeGcBehavior::NoSafepoint;

  spec.resources.push_back(NativeResourceUse{NativeResourceKind::File,
                                             NativeResourceAccess::InputOutput,
                                             NativeOwnershipRule::Borrow,
                                             NativeCleanupBehavior::CloseRequired,
                                             0});
  auto resource = AnalyzeNativeJitCall(spec, {TypeKind::String, TypeKind::I32}, TypeKind::I32);
  return !resource.jit_loop_safe && resource.reason == "resource-argument-or-result";
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
  if (layout.padding.size() != 1 || layout.padding[0].offset != 1 ||
      layout.padding[0].size != 3 || !layout.padding[0].zero_initialized) {
    return false;
  }
  if (layout.size != 16 || layout.align != 8 || !layout.pass_by_value) return false;
  if (!layout.native_callable || layout.external_ffi_callable || layout.contains_references) {
    return false;
  }

  const auto ref_layout = ComputeStableAggregateLayout({
      GetPrimitiveAbiTypeInfo(TypeKind::I32),
      GetPrimitiveAbiTypeInfo(TypeKind::String),
  });
  return ref_layout.fields.size() == 2 && ref_layout.fields[1].offset == 8 &&
         ref_layout.padding.size() == 1 && ref_layout.padding[0].offset == 4 &&
         ref_layout.padding[0].size == 4 && ref_layout.size == 16 &&
         ref_layout.contains_references && !ref_layout.pass_by_value &&
         !ref_layout.external_ffi_callable;
}

const TestCase kVmRuntimeAbiTests[] = {
  {"vm_runtime_abi_mangles_generic_symbols", VmRuntimeAbiManglesGenericSymbols},
  {"vm_runtime_abi_builds_canonical_type_identities", VmRuntimeAbiBuildsCanonicalTypeIdentities},
  {"vm_runtime_abi_maps_primitive_types", VmRuntimeAbiMapsPrimitiveTypes},
  {"vm_runtime_abi_validates_scalar_values", VmRuntimeAbiValidatesScalarValues},
  {"vm_runtime_abi_maps_opaque_handle_type_rows", VmRuntimeAbiMapsOpaqueHandleTypeRows},
  {"vm_runtime_abi_maps_stable_sbc_data_types", VmRuntimeAbiMapsStableSbcDataTypes},
  {"vm_runtime_abi_maps_enum_underlying_types", VmRuntimeAbiMapsEnumUnderlyingTypes},
  {"vm_runtime_abi_aligns_stable_data_fields", VmRuntimeAbiAlignsStableDataFields},
  {"vm_runtime_promise_registry_tracks_states", VmRuntimePromiseRegistryTracksStates},
  {"vm_runtime_promise_registry_waits_across_threads", VmRuntimePromiseRegistryWaitsAcrossThreads},
  {"vm_runtime_abi_packs_promise_ids", VmRuntimeAbiPacksPromiseIds},
  {"vm_runtime_abi_builds_result_and_optional_values", VmRuntimeAbiBuildsResultAndOptionalValues},
  {"vm_runtime_abi_classifies_pass_modes", VmRuntimeAbiClassifiesPassModes},
  {"vm_runtime_abi_validates_borrowed_views", VmRuntimeAbiValidatesBorrowedViews},
  {"vm_runtime_abi_marks_opaque_vm_references", VmRuntimeAbiMarksOpaqueVmReferences},
  {"vm_runtime_abi_accepts_explicit_external_wrappers", VmRuntimeAbiAcceptsExplicitExternalWrappers},
  {"vm_runtime_abi_validates_external_c_signatures", VmRuntimeAbiValidatesExternalCSignatures},
  {"vm_runtime_abi_validates_callable_signatures", VmRuntimeAbiValidatesCallableSignatures},
  {"vm_runtime_abi_computes_stable_layout_hashes", VmRuntimeAbiComputesStableLayoutHashes},
  {"vm_runtime_abi_computes_nested_aggregate_layout", VmRuntimeAbiComputesNestedAggregateLayout},
  {"vm_runtime_abi_computes_fixed_array_layout", VmRuntimeAbiComputesFixedArrayLayout},
  {"vm_runtime_abi_data_methods_do_not_affect_layout", VmRuntimeAbiDataMethodsDoNotAffectLayout},
  {"vm_runtime_abi_rejects_recursive_value_containment", VmRuntimeAbiRejectsRecursiveValueContainment},
  {"vm_runtime_abi_validates_dynamic_dl_abi", VmRuntimeAbiValidatesDynamicDlAbi},
  {"vm_runtime_abi_classifies_native_jit_calls", VmRuntimeAbiClassifiesNativeJitCalls},
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
