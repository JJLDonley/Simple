#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "native/fs.h"

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

const TestCase kVmNativeFsTests[] = {
  {"vm_runtime_dispatches_registered_natives_by_metadata_first", VmRuntimeDispatchesRegisteredNativesByMetadataFirst},
  {"vm_dynamic_dl_dispatch_lives_in_ffi_module", VmDynamicDlDispatchLivesInFfiModule},
  {"vm_runtime_has_no_native_stdlib_forwarding_glue", VmRuntimeHasNoNativeStdlibForwardingGlue},
  {"vm_native_registry_uses_named_metadata_handlers", VmNativeRegistryUsesNamedMetadataHandlers},
  {"vm_split_native_fs_writes_reads_and_removes_text", VmSplitNativeFsWritesReadsAndRemovesText},
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
