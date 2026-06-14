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

bool VmInterpreterModuleExcludesNativeSubsystems() {
  const std::array<const char*, 4> paths = {
      "VM/src/interpreter/interpreter.cpp",
      "VM/src/interpreter/dispatch.cpp",
      "VM/src/interpreter/frames.cpp",
      "VM/src/interpreter/stack.cpp",
  };
  const char* forbidden[] = {
      "native/",
      "ffi/",
      "json",
      "Channel",
      "std::filesystem",
      "<filesystem>",
  };
  for (const char* path : paths) {
    std::ifstream in(path);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (const char* token : forbidden) {
      if (text.find(token) != std::string::npos) return false;
    }
  }
  return true;
}

bool VmInterpreterModuleOwnsOpcodeLoopBoundaries() {
  std::ifstream in("VM/include/interpreter/interpreter.h");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("struct InterpreterState") != std::string::npos &&
         text.find("native/") == std::string::npos &&
         text.find("ffi/") == std::string::npos;
}

bool VmFrameSetupUsesNamedHelper() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("FrameState BuildInterpreterFrame(") != std::string::npos &&
         text.find("auto setup_frame = [") == std::string::npos;
}

bool VmRuntimeLimitAndLocalAllocationUseNamedHelpers() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("bool CheckRuntimeSequenceLimit(") != std::string::npos &&
         text.find("size_t AllocateLocalSlots(") != std::string::npos &&
         text.find("auto check_sequence_limit = [") == std::string::npos &&
         text.find("auto alloc_locals = [") == std::string::npos;
}

bool VmConstantAndGlobalLookupsUseNamedHelpers() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("bool LoadConstStringSlot(") != std::string::npos &&
         text.find("bool IsRefLikeGlobal(") != std::string::npos &&
         text.find("auto read_const_string = [") == std::string::npos &&
         text.find("auto is_ref_like_global = [") == std::string::npos;
}

bool VmExecutionStatsUseNamedHelper() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("ExecResult AttachExecutionStats(") != std::string::npos &&
         text.find("auto finish = [") == std::string::npos;
}

bool VmJitFailureUsesNamedOperandHelpers() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const size_t jit_fail = text.find("auto jit_fail = [");
  if (jit_fail == std::string::npos) return false;
  const size_t loop = text.find("while (pc < end_pc)", jit_fail);
  if (loop == std::string::npos) return false;
  const std::string body = text.substr(jit_fail, loop - jit_fail);
  return body.find("auto read_u32 = [") == std::string::npos &&
         body.find("auto read_i32 = [") == std::string::npos &&
         body.find("ReadU32Operand(module.code") != std::string::npos &&
         body.find("ReadI32Operand(module.code") != std::string::npos;
}

bool VmTrapFormattingUsesNamedHelpers() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("std::string LookupMethodName(") != std::string::npos &&
         text.find("bool ReadU32Operand(") != std::string::npos &&
         text.find("bool ReadI32Operand(") != std::string::npos &&
         text.find("auto get_method_name = [") == std::string::npos;
}

bool VmImportDispatcherUsesNamedFunction() {
  std::ifstream in("VM/src/vm.cpp");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("bool DispatchImportCallByName(") != std::string::npos &&
         text.find("auto handle_import_call = [") == std::string::npos;
}

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
  {"vm_interpreter_module_excludes_native_subsystems", VmInterpreterModuleExcludesNativeSubsystems},
  {"vm_interpreter_module_owns_opcode_loop_boundaries", VmInterpreterModuleOwnsOpcodeLoopBoundaries},
  {"vm_frame_setup_uses_named_helper", VmFrameSetupUsesNamedHelper},
  {"vm_runtime_limit_and_local_allocation_use_named_helpers", VmRuntimeLimitAndLocalAllocationUseNamedHelpers},
  {"vm_constant_and_global_lookups_use_named_helpers", VmConstantAndGlobalLookupsUseNamedHelpers},
  {"vm_execution_stats_use_named_helper", VmExecutionStatsUseNamedHelper},
  {"vm_jit_failure_uses_named_operand_helpers", VmJitFailureUsesNamedOperandHelpers},
  {"vm_trap_formatting_uses_named_helpers", VmTrapFormattingUsesNamedHelpers},
  {"vm_import_dispatcher_uses_named_function", VmImportDispatcherUsesNamedFunction},
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
