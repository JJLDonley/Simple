#include "test_utils.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#include "native/dispatch.h"
#include "native/fs.h"
#include "native/registry.h"
#include "native/resource_registry.h"

namespace Simple::VM::Tests {
namespace {

bool VmRuntimeDispatchesRegisteredNativesByMetadataFirst() {
  std::ifstream vm("VM/src/vm.cpp");
  std::ifstream native_header("VM/include/native/dispatch.h");
  std::ifstream native_source("VM/src/native/dispatch.cpp");
  std::ifstream import_source("VM/src/runtime/import_dispatch.cpp");
  if (!vm || !native_header || !native_source || !import_source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string native_header_text((std::istreambuf_iterator<char>(native_header)), std::istreambuf_iterator<char>());
  const std::string native_source_text((std::istreambuf_iterator<char>(native_source)), std::istreambuf_iterator<char>());
  const std::string import_source_text((std::istreambuf_iterator<char>(import_source)), std::istreambuf_iterator<char>());
  const size_t dispatch = import_source_text.find("Simple::VM::Native::DispatchMetadataImport(native_registry");
  const size_t dl_call = import_source_text.find("if (mod == \"System.dl\")");
  return dispatch != std::string::npos && dl_call != std::string::npos && dispatch < dl_call &&
         native_header_text.find("struct MetadataDispatchContext") != std::string::npos &&
         native_source_text.find("bool DispatchMetadataImport(") != std::string::npos &&
         vm_text.find("bool DispatchNativeMetadataImport(") == std::string::npos &&
         vm_text.find("struct NativeMetadataDispatchContext") == std::string::npos;
}

bool VmDynamicDlDispatchLivesInFfiModule() {
  std::ifstream vm("VM/src/vm.cpp");
  std::ifstream header("VM/include/ffi/dl_call.h");
  std::ifstream source("VM/src/ffi/dl_call.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("DispatchDynamicDlCall(") != std::string::npos &&
         source_text.find("bool DispatchDynamicDlCall(") != std::string::npos &&
         source_text.find("struct DlAbiCache") != std::string::npos &&
         source_text.find("ConvertDlArg") != std::string::npos &&
         vm_text.find("bool DispatchDynamicDlCall(") == std::string::npos &&
         vm_text.find("struct DlAbiCache") == std::string::npos &&
         vm_text.find("ConvertDlArg") == std::string::npos;
}

bool VmRuntimeHasNoNativeStdlibForwardingGlue() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const char* forbidden[] = {
      "if (mod == \"System.os\")",
      "if (mod == \"System.fs\")",
      "if (mod == \"System.channel\")",
      "if (mod == \"System.json\")",
      "if (mod == \"System.buffer\")",
      "if (mod == \"System.log\")",
  };
  for (const char* item : forbidden) {
    if (text.find(item) != std::string::npos) return false;
  }
  return true;
}

bool VmNativeRegistryUsesNamedMetadataHandlers() {
  std::ifstream in("VM/src/native/registry.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.find("FsReadText") == std::string::npos) return false;
  if (text.find("ChannelPendingI32") == std::string::npos) return false;
  if (text.find("JsonParse") == std::string::npos) return false;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find("MakeSpec(") != std::string::npos && line.find('[') != std::string::npos) {
      return false;
    }
  }
  return true;
}

bool VmSplitNativeFsWritesReadsAndRemovesText() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "simple_vm_native_fs_split_test.txt";
  const std::string text = "split fs test";
  std::string read;
  if (!Simple::VM::Native::Fs::WriteText(path.string(), text)) return false;
  if (!Simple::VM::Native::Fs::ReadText(path.string(), &read)) return false;
  const bool removed = Simple::VM::Native::Fs::Remove(path.string());
  return removed && read == text && !std::filesystem::exists(path);
}

bool VmNativeRegistryMetadataValidatesSpecs() {
  Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  std::string error;
  if (!Simple::VM::Native::ValidateNativeRegistryMetadata(registry, &error) || !error.empty()) {
    return false;
  }

  Simple::VM::Native::NativeFunctionSpec bad;
  bad.module_name = "System.bad";
  bad.symbol_name = "leak";
  bad.parameter_types = {Simple::Byte::TypeKind::I32};
  bad.result_type = Simple::Byte::TypeKind::I32;
  bad.resources.push_back(Simple::VM::Native::NativeResourceUse{
      Simple::VM::Native::NativeResourceKind::File,
      Simple::VM::Native::NativeResourceAccess::Output,
      Simple::VM::Native::NativeOwnershipRule::TransferToCaller,
      Simple::VM::Native::NativeCleanupBehavior::None,
      0xffffffffu});
  bad.handler = [](Simple::VM::Native::NativeCallContext&) {
    Simple::VM::Native::NativeCallResult result;
    result.value = 0;
    return result;
  };
  if (!registry.Register(std::move(bad))) return false;
  error.clear();
  if (Simple::VM::Native::ValidateNativeRegistryMetadata(registry, &error) ||
      error.find("resource output missing cleanup behavior") == std::string::npos) {
    return false;
  }

  Simple::VM::Native::NativeRegistry abi_registry;
  Simple::VM::Native::NativeFunctionSpec bad_abi;
  bad_abi.module_name = "System.bad";
  bad_abi.symbol_name = "voidParam";
  bad_abi.parameter_types = {Simple::Byte::TypeKind::Void};
  bad_abi.result_type = Simple::Byte::TypeKind::I32;
  bad_abi.handler = [](Simple::VM::Native::NativeCallContext&) {
    return Simple::VM::Native::NativeCallResult::I32(0);
  };
  if (!abi_registry.Register(std::move(bad_abi))) return false;
  error.clear();
  return !Simple::VM::Native::ValidateNativeRegistryMetadata(abi_registry, &error) &&
         error.find("invalid ABI signature") != std::string::npos;
}

bool VmNativeCallContextTypedAccessorsAndBuildersWork() {
  Simple::VM::Heap heap;
  const uint32_t text_ref = Simple::VM::CreateString(heap, u"typed");
  const uint32_t bytes_ref = Simple::VM::CreateBytes(heap, {4, 5, 6});
  float f32_input = 1.25f;
  uint32_t f32_bits = 0;
  std::memcpy(&f32_bits, &f32_input, sizeof(f32_bits));
  double f64_input = 2.5;
  uint64_t f64_bits = 0;
  std::memcpy(&f64_bits, &f64_input, sizeof(f64_bits));
  Simple::VM::Native::NativeCallContext context;
  context.heap = &heap;
  context.args = {42, 9000000000ull, f32_bits, f64_bits, text_ref};

  int32_t i32 = 0;
  int64_t i64 = 0;
  float f32 = 0.0f;
  double f64 = 0.0;
  uint32_t ref = 0;
  Simple::VM::Native::NativeHandleId handle_arg;
  std::string text;
  Simple::VM::Runtime::SimpleStringView string_view;
  if (!context.ArgI32(0, &i32) || i32 != 42) return false;
  if (!context.ArgI64(1, &i64) || i64 != 9000000000ll) return false;
  if (!context.ArgF32(2, &f32) || f32 != f32_input) return false;
  if (!context.ArgF64(3, &f64) || f64 != f64_input) return false;
  if (!context.ArgRef(4, &ref) || ref != text_ref) return false;
  context.args.push_back(bytes_ref);
  Simple::VM::Runtime::SimpleBytesView bytes_view;
  if (!context.ArgBytesView(5, &bytes_view) || bytes_view.size != 3 ||
      bytes_view.data[0] != 4 || bytes_view.data[2] != 6) {
    return false;
  }
  context.args.push_back(Simple::VM::Native::PackNativeHandleId({7, 3}));
  if (!context.ArgHandle(6, &handle_arg) || handle_arg.index != 7 || handle_arg.generation != 3) {
    return false;
  }
  if (!context.ArgString(4, &text) || text != "typed") return false;
  if (!context.ArgStringView(4, &string_view) || string_view.size != 5 ||
      string_view.encoding != Simple::VM::Runtime::AbiStringEncoding::Utf8 ||
      std::string(string_view.data, string_view.size) != "typed") {
    return false;
  }
  if (context.ArgI32(9, &i32)) return false;

  const auto void_result = Simple::VM::Native::NativeCallResult::Void();
  const auto i32_result = Simple::VM::Native::NativeCallResult::I32(-7);
  const auto i64_result = Simple::VM::Native::NativeCallResult::I64(-99);
  const auto f32_result = Simple::VM::Native::NativeCallResult::F32(f32_input);
  const auto f64_result = Simple::VM::Native::NativeCallResult::F64(f64_input);
  const auto ref_result = Simple::VM::Native::NativeCallResult::Ref(text_ref);
  const auto handle_result = Simple::VM::Native::NativeCallResult::Handle({9, 4});
  const auto string_result = Simple::VM::Native::NativeCallResult::String("ok");
  const auto error_result = Simple::VM::Native::NativeCallResult::Error("bad");
  return !void_result.has_value && i32_result.value == static_cast<uint32_t>(-7) &&
         static_cast<int64_t>(i64_result.value) == -99 && f32_result.value == f32_bits &&
         f64_result.value == f64_bits && ref_result.value == text_ref &&
         handle_result.value == Simple::VM::Native::PackNativeHandleId({9, 4}) &&
         string_result.string_value == "ok" && !error_result.ok && error_result.error == "bad";
}

bool HasCapability(const Simple::VM::Native::NativeFunctionSpec* spec, const std::string& tag) {
  if (!spec) return false;
  for (const std::string& candidate : spec->capability_tags) {
    if (candidate == tag) return true;
  }
  return false;
}

bool VmNativeFunctionMetadataDeclaresCapabilities() {
  const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  return HasCapability(registry.Find("System.env", "get"), "environment.read") &&
         HasCapability(registry.Find("System.env", "set"), "environment.write") &&
         HasCapability(registry.Find("System.env", "argsCount"), "process.args") &&
         HasCapability(registry.Find("System.os", "time_mono_ns"), "clock.time") &&
         HasCapability(registry.Find("System.os", "sleep_ms"), "threading") &&
         HasCapability(registry.Find("System.thread", "yield"), "threading") &&
         HasCapability(registry.Find("System.random", "i32"), "randomness");
}

bool VmNativeFunctionMetadataDeclaresStability() {
  using Simple::VM::Native::NativeStability;

  const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  const auto* dl_open = registry.Find("System.dl", "open");
  const auto* dl_sym = registry.Find("System.dl", "sym");
  const auto* dl_close = registry.Find("System.dl", "close");
  const auto* env_platform = registry.Find("System.env", "platform");
  const auto* env_arch = registry.Find("System.env", "arch");
  return dl_open && dl_open->stability == NativeStability::Unsafe &&
         dl_sym && dl_sym->stability == NativeStability::Unsafe &&
         dl_close && dl_close->stability == NativeStability::Unsafe &&
         env_platform && env_platform->stability == NativeStability::Stable &&
         env_arch && env_arch->stability == NativeStability::Stable;
}

bool VmNativeFunctionMetadataDeclaresResources() {
  using Simple::VM::Native::NativeBlockingBehavior;
  using Simple::VM::Native::NativeCleanupBehavior;
  using Simple::VM::Native::NativeOwnershipRule;
  using Simple::VM::Native::NativeResourceAccess;
  using Simple::VM::Native::NativeResourceKind;

  const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  const auto* fs_open = registry.Find("System.fs", "open");
  const auto* fs_read = registry.Find("System.fs", "read");
  const auto* fs_close = registry.Find("System.fs", "close");
  const auto* dl_open = registry.Find("System.dl", "open");
  const auto* dl_sym = registry.Find("System.dl", "sym");
  if (!fs_open || !fs_read || !fs_close || !dl_open || !dl_sym) return false;
  if (fs_open->resources.size() != 1 ||
      fs_open->resources[0].kind != NativeResourceKind::File ||
      fs_open->resources[0].access != NativeResourceAccess::Output ||
      fs_open->resources[0].ownership != NativeOwnershipRule::TransferToCaller ||
      fs_open->resources[0].cleanup != NativeCleanupBehavior::AutoCloseOnVmShutdown ||
      fs_open->blocking != NativeBlockingBehavior::MayBlock) {
    return false;
  }
  if (fs_read->resources.size() != 1 ||
      fs_read->resources[0].kind != NativeResourceKind::File ||
      fs_read->resources[0].access != NativeResourceAccess::Input ||
      fs_read->resources[0].ownership != NativeOwnershipRule::Borrow ||
      fs_read->resources[0].cleanup != NativeCleanupBehavior::None ||
      fs_read->resources[0].parameter_index != 0 ||
      fs_read->capability_tags.empty()) {
    return false;
  }
  if (fs_close->resources.size() != 1 ||
      fs_close->resources[0].access != NativeResourceAccess::InputOutput ||
      fs_close->resources[0].ownership != NativeOwnershipRule::TransferToCallee ||
      fs_close->resources[0].cleanup != NativeCleanupBehavior::CloseRequired) {
    return false;
  }
  return dl_open->resources.size() == 1 &&
         dl_open->resources[0].kind == NativeResourceKind::FfiLibrary &&
         dl_open->resources[0].access == NativeResourceAccess::Output &&
         dl_sym->resources.size() == 1 &&
         dl_sym->resources[0].parameter_index == 0 &&
         !dl_open->capability_tags.empty();
}

bool VmNativeGeneratedDocsIncludeCapabilitiesAndResources() {
  const Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  const std::string docs = Simple::VM::Native::GenerateStdLibMarkdown(registry);
  return docs.find("| Symbol | Signature | Blocking | Capabilities | Resources | Platforms | Stability | Summary |") !=
             std::string::npos &&
         docs.find("| `readText` | `(string) -> string` | `may-block` | `filesystem.read` | `-` | `all` | `experimental` | Read a UTF-8 text file. |") !=
             std::string::npos &&
         docs.find("| `open` | `(string, i32) -> i32` | `may-block` | `filesystem.open` | `out:file:to-caller:vm-shutdown` | `all` | `experimental` | Open a file descriptor handle. |") !=
             std::string::npos &&
         docs.find("| `sym` | `(i64, string) -> i64` | `non-blocking` | `ffi.dynamic_load` | `in:ffi-library@0:borrow:none` | `all` | `unsafe` | Resolve a symbol from a dynamic library handle. |") !=
             std::string::npos;
}

bool VmNativeDispatchValidatesResourceHandles() {
  using Simple::VM::Native::NativeCleanupBehavior;
  using Simple::VM::Native::NativeFunctionSpec;
  using Simple::VM::Native::NativeHandleId;
  using Simple::VM::Native::NativeOwnershipRule;
  using Simple::VM::Native::NativeRegistry;
  using Simple::VM::Native::NativeResourceAccess;
  using Simple::VM::Native::NativeResourceKind;
  using Simple::VM::Native::NativeResourceRecord;
  using Simple::VM::Native::NativeResourceRegistry;
  using Simple::VM::Native::NativeResourceUse;

  int calls = 0;
  NativeRegistry registry;
  NativeFunctionSpec spec;
  spec.module_name = "System.test";
  spec.symbol_name = "useFile";
  spec.parameter_types = {Simple::Byte::TypeKind::I64};
  spec.result_type = Simple::Byte::TypeKind::I32;
  spec.resources.push_back(NativeResourceUse{NativeResourceKind::File, NativeResourceAccess::Input,
                                             NativeOwnershipRule::Borrow,
                                             NativeCleanupBehavior::None, 0});
  spec.handler = [&calls](Simple::VM::Native::NativeCallContext& context) {
    ++calls;
    Simple::VM::Native::NativeHandleId handle;
    if (!context.resource_registry || !context.ArgHandle(0, &handle) || handle.IsNull()) {
      return Simple::VM::Native::NativeCallResult::Error("missing handle");
    }
    return Simple::VM::Native::NativeCallResult::I32(1);
  };
  if (!registry.Register(std::move(spec))) return false;

  NativeResourceRegistry resources;
  NativeResourceRecord file;
  file.kind = NativeResourceKind::File;
  const NativeHandleId file_handle = resources.Insert(file);
  NativeResourceRecord socket;
  socket.kind = NativeResourceKind::Socket;
  const NativeHandleId socket_handle = resources.Insert(socket);

  Simple::VM::Heap heap;
  Simple::VM::Native::MetadataDispatchContext context;
  context.heap = &heap;
  context.resource_registry = &resources;
  uint64_t ret = 0;
  bool has_ret = false;
  std::string error;
  bool handled = Simple::VM::Native::DispatchMetadataImport(
      registry, "System.test", "useFile", {Simple::VM::Native::PackNativeHandleId(file_handle)},
      Simple::Byte::TypeKind::I32, context, &ret, &has_ret, &error);
  if (!handled || !error.empty() || ret != 1 || calls != 1) return false;

  handled = Simple::VM::Native::DispatchMetadataImport(
      registry, "System.test", "useFile", {Simple::VM::Native::PackNativeHandleId(socket_handle)},
      Simple::Byte::TypeKind::I32, context, &ret, &has_ret, &error);
  return handled && calls == 1 && error.find("wrong resource kind") != std::string::npos;
}

bool VmNativeDispatchEnforcesCapabilities() {
  Simple::VM::Native::NativeRegistry registry = Simple::VM::Native::BuildDefaultRegistry();
  Simple::VM::Heap heap;
  const uint64_t path_ref = Simple::VM::CreateString(heap, u"missing.txt");
  Simple::VM::Native::CapabilityPolicy policy;
  policy.allow_all = false;

  Simple::VM::Native::MetadataDispatchContext context;
  context.heap = &heap;
  context.capability_policy = &policy;
  uint64_t ret = 0;
  bool has_ret = true;
  std::string error;
  bool handled = Simple::VM::Native::DispatchMetadataImport(
      registry, "System.fs", "readText", {path_ref}, Simple::Byte::TypeKind::String,
      context, &ret, &has_ret, &error);
  if (!handled || error.find("denied capability: filesystem.read") == std::string::npos) {
    return false;
  }

  policy.allowed_tags.push_back("filesystem.read");
  error.clear();
  handled = Simple::VM::Native::DispatchMetadataImport(
      registry, "System.fs", "readText", {path_ref}, Simple::Byte::TypeKind::String,
      context, &ret, &has_ret, &error);
  return handled && error.empty();
}

bool VmNativeResourceRegistryReportsShutdownFailures() {
  using Simple::VM::Native::NativeResourceKind;
  using Simple::VM::Native::NativeResourceRecord;
  using Simple::VM::Native::NativeResourceRegistry;

  int close_count = 0;
  int finalize_count = 0;
  NativeResourceRegistry registry;
  NativeResourceRecord record;
  record.kind = NativeResourceKind::File;
  record.payload = &close_count;
  record.close = [](void* payload, std::string* error) -> bool {
    ++*static_cast<int*>(payload);
    if (error) *error = "close failed for test";
    return false;
  };
  record.finalize = [](void* payload) {
    // The close counter address is reused as a sentinel payload. Finalize does
    // not own it; this test only verifies that finalization still runs.
    (void)payload;
  };
  registry.Insert(record);

  NativeResourceRecord second;
  second.kind = NativeResourceKind::Buffer;
  second.payload = &finalize_count;
  second.close = [](void*, std::string*) -> bool { return true; };
  second.finalize = [](void* payload) { ++*static_cast<int*>(payload); };
  registry.Insert(second);

  const size_t failed = registry.SweepShutdown();
  return failed == 1 && close_count == 1 && finalize_count == 1 && registry.LiveCount() == 0;
}

bool VmNativeResourceRegistryTracksHandleLifecycle() {
  using Simple::VM::Native::NativeHandleId;
  using Simple::VM::Native::NativeResourceKind;
  using Simple::VM::Native::NativeResourceRecord;
  using Simple::VM::Native::NativeResourceRegistry;
  using Simple::VM::Native::NativeResourceStatus;

  int close_count = 0;
  NativeResourceRegistry registry;
  NativeResourceRecord record;
  record.kind = NativeResourceKind::File;
  record.debug_label = "unit-file";
  record.payload = &close_count;
  record.close = [](void* payload, std::string*) -> bool {
    ++*static_cast<int*>(payload);
    return true;
  };

  const NativeHandleId handle = registry.Insert(record);
  if (registry.LiveCount() != 1) return false;
  if (Simple::VM::Native::UnpackNativeHandleId(Simple::VM::Native::PackNativeHandleId(handle)).generation !=
      handle.generation) {
    return false;
  }

  NativeResourceRecord* found = nullptr;
  if (registry.Get(handle, NativeResourceKind::File, &found) != NativeResourceStatus::Ok || !found) {
    return false;
  }
  if (registry.Get(handle, NativeResourceKind::Socket, nullptr) != NativeResourceStatus::WrongKind) {
    return false;
  }
  if (registry.Close(handle, NativeResourceKind::File, nullptr) != NativeResourceStatus::Ok) return false;
  if (close_count != 1 || registry.LiveCount() != 0) return false;
  if (registry.Close(handle, NativeResourceKind::File, nullptr) != NativeResourceStatus::AlreadyClosed) {
    return false;
  }

  const NativeHandleId reused = registry.Insert(record);
  if (reused.index != handle.index || reused.generation == handle.generation) return false;
  if (registry.Get(handle, NativeResourceKind::File, nullptr) != NativeResourceStatus::StaleHandle) {
    return false;
  }
  registry.SweepShutdown();
  return close_count == 2 && registry.LiveCount() == 0;
}

const TestCase kVmNativeFsTests[] = {
  {"vm_runtime_dispatches_registered_natives_by_metadata_first", VmRuntimeDispatchesRegisteredNativesByMetadataFirst},
  {"vm_dynamic_dl_dispatch_lives_in_ffi_module", VmDynamicDlDispatchLivesInFfiModule},
  {"vm_runtime_has_no_native_stdlib_forwarding_glue", VmRuntimeHasNoNativeStdlibForwardingGlue},
  {"vm_native_registry_uses_named_metadata_handlers", VmNativeRegistryUsesNamedMetadataHandlers},
  {"vm_split_native_fs_writes_reads_and_removes_text", VmSplitNativeFsWritesReadsAndRemovesText},
  {"vm_native_call_context_typed_accessors_and_builders_work", VmNativeCallContextTypedAccessorsAndBuildersWork},
  {"vm_native_registry_metadata_validates_specs", VmNativeRegistryMetadataValidatesSpecs},
  {"vm_native_function_metadata_declares_capabilities", VmNativeFunctionMetadataDeclaresCapabilities},
  {"vm_native_function_metadata_declares_stability", VmNativeFunctionMetadataDeclaresStability},
  {"vm_native_function_metadata_declares_resources", VmNativeFunctionMetadataDeclaresResources},
  {"vm_native_generated_docs_include_capabilities_and_resources", VmNativeGeneratedDocsIncludeCapabilitiesAndResources},
  {"vm_native_dispatch_validates_resource_handles", VmNativeDispatchValidatesResourceHandles},
  {"vm_native_dispatch_enforces_capabilities", VmNativeDispatchEnforcesCapabilities},
  {"vm_native_resource_registry_reports_shutdown_failures", VmNativeResourceRegistryReportsShutdownFailures},
  {"vm_native_resource_registry_tracks_handle_lifecycle", VmNativeResourceRegistryTracksHandleLifecycle},
};

const TestSection kVmNativeFsSections[] = {
  {"vm_native_fs", kVmNativeFsTests, sizeof(kVmNativeFsTests) / sizeof(kVmNativeFsTests[0])},
};

} // namespace

const TestSection* GetVmNativeFsSections(size_t* count) {
  if (count) *count = sizeof(kVmNativeFsSections) / sizeof(kVmNativeFsSections[0]);
  return kVmNativeFsSections;
}

} // namespace Simple::VM::Tests
