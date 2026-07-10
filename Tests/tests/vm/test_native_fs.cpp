#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

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

bool VmNativeFunctionMetadataDeclaresResources() {
  using Simple::VM::Native::NativeBlockingBehavior;
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
      fs_open->blocking != NativeBlockingBehavior::MayBlock) {
    return false;
  }
  if (fs_read->resources.size() != 1 ||
      fs_read->resources[0].kind != NativeResourceKind::File ||
      fs_read->resources[0].access != NativeResourceAccess::Input ||
      fs_read->resources[0].parameter_index != 0 ||
      fs_read->capability_tags.empty()) {
    return false;
  }
  if (fs_close->resources.size() != 1 ||
      fs_close->resources[0].access != NativeResourceAccess::InputOutput) {
    return false;
  }
  return dl_open->resources.size() == 1 &&
         dl_open->resources[0].kind == NativeResourceKind::FfiLibrary &&
         dl_open->resources[0].access == NativeResourceAccess::Output &&
         dl_sym->resources.size() == 1 &&
         dl_sym->resources[0].parameter_index == 0 &&
         !dl_open->capability_tags.empty();
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
  {"vm_native_function_metadata_declares_resources", VmNativeFunctionMetadataDeclaresResources},
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
