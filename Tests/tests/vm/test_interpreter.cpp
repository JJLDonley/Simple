#include "test_utils.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "interpreter/frames.h"
#include "interpreter/stack.h"

namespace Simple::VM::Tests {
namespace {

bool VmRuntimeSplitModulesExist() {
  const std::array<const char*, 19> paths = {
      "VM/src/interpreter/interpreter.cpp",
      "VM/src/interpreter/dispatch.cpp",
      "VM/src/interpreter/frames.cpp",
      "VM/src/interpreter/stack.cpp",
      "VM/src/native/registry.cpp",
      "VM/src/native/os.cpp",
      "VM/src/native/fs.cpp",
      "VM/src/native/path.cpp",
      "VM/src/native/env.cpp",
      "VM/src/native/time.cpp",
      "VM/src/native/random.cpp",
      "VM/src/native/log.cpp",
      "VM/src/native/channel.cpp",
      "VM/src/native/buffer.cpp",
      "VM/src/native/json.cpp",
      "VM/src/native/thread.cpp",
      "VM/src/ffi/dl_runtime.cpp",
      "VM/src/jit/jit_scaffold.cpp",
      "VM/src/gc/root_tracer.cpp",
  };
  for (const char* path : paths) {
    if (!std::filesystem::exists(path)) return false;
  }
  return std::filesystem::exists("VM/src/runtime/runtime_limits.cpp");
}

bool VmBoundaryTypesAreExplicit() {
  const std::array<const char*, 6> paths = {
      "VM/include/native/registry.h",
      "VM/include/interpreter/frames.h",
      "VM/include/interpreter/interpreter.h",
      "VM/include/gc/root_tracer.h",
      "VM/include/runtime/runtime_limits.h",
      "VM/include/ffi/dl_runtime.h",
  };
  std::string combined;
  for (const char* path : paths) {
    std::ifstream in(path);
    if (!in) return false;
    combined.append(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
  }
  const char* required[] = {
      "struct NativeCallContext",
      "struct NativeCallResult",
      "struct NativeModule",
      "struct NativeFunction",
      "struct FrameState",
      "struct InterpreterState",
      "struct RootTraceContext",
  };
  for (const char* token : required) {
    if (combined.find(token) == std::string::npos) return false;
  }
  return true;
}

bool VmSplitInterpreterStackAndFrames() {
  std::vector<Simple::VM::Interpreter::Slot> stack;
  Simple::VM::Interpreter::Push(stack, 11);
  Simple::VM::Interpreter::Push(stack, 22);
  if (Simple::VM::Interpreter::Peek(stack) != 22) return false;
  if (Simple::VM::Interpreter::Pop(stack) != 22) return false;
  if (Simple::VM::Interpreter::Pop(stack) != 11) return false;

  const auto frame = Simple::VM::Interpreter::MakeFrame(3, 9, 4, 17);
  return frame.func_index == 3 && frame.return_pc == 9 &&
         frame.stack_base == 4 && frame.closure_ref == 17;
}

const TestCase kVmInterpreterTests[] = {
  {"vm_runtime_split_modules_exist", VmRuntimeSplitModulesExist},
  {"vm_boundary_types_are_explicit", VmBoundaryTypesAreExplicit},
  {"vm_split_interpreter_stack_and_frames", VmSplitInterpreterStackAndFrames},
};

const TestSection kVmInterpreterSections[] = {
  {"vm_interpreter", kVmInterpreterTests, sizeof(kVmInterpreterTests) / sizeof(kVmInterpreterTests[0])},
};

} // namespace

const TestSection* GetVmInterpreterSections(size_t* count) {
  if (count) *count = sizeof(kVmInterpreterSections) / sizeof(kVmInterpreterSections[0]);
  return kVmInterpreterSections;
}

} // namespace Simple::VM::Tests
