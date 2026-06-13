#include "test_utils.h"

#include <filesystem>
#include <string>

#include "native/fs.h"

namespace Simple::VM::Tests {
namespace {

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
