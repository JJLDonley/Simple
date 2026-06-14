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
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const size_t dispatch = text.find("DispatchNativeMetadataImport(native_registry");
  const size_t legacy = text.find("if (mod == \"System.os\")");
  return dispatch != std::string::npos && legacy != std::string::npos && dispatch < legacy;
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
