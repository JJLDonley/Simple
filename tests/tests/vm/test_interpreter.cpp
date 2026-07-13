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
      "source/VM/src/interpreter/interpreter.cpp",
      "source/VM/src/interpreter/dispatch.cpp",
      "source/VM/src/interpreter/frames.cpp",
      "source/VM/src/interpreter/stack.cpp",
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
  std::ifstream in("source/VM/include/interpreter/interpreter.h");
  if (!in) return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return text.find("struct InterpreterState") != std::string::npos &&
         text.find("native/") == std::string::npos &&
         text.find("ffi/") == std::string::npos;
}

bool VmGcStackMapCollectionLivesInGcModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/gc/stack_map_collection.h");
  std::ifstream source("source/VM/src/gc/stack_map_collection.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("MaybeCollectWithStackMap(") != std::string::npos &&
         source_text.find("FindVerifiedStackMap(") != std::string::npos &&
         vm_text.find("const Simple::Byte::StackMap* FindVerifiedStackMap(") == std::string::npos &&
         vm_text.find("void MaybeCollectWithStackMap(") == std::string::npos &&
         vm_text.find("auto find_stack_map = [") == std::string::npos &&
         vm_text.find("auto maybe_collect = [") == std::string::npos;
}

bool VmFrameSetupLivesInInterpreterModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/interpreter/frames.h");
  std::ifstream source("source/VM/src/interpreter/frames.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("FrameState BuildFrame(") != std::string::npos &&
         source_text.find("FrameState BuildFrame(") != std::string::npos &&
         source_text.find("size_t AllocateLocalSlots(") != std::string::npos &&
         vm_text.find("BuildInterpreterFrame(") == std::string::npos &&
         vm_text.find("size_t AllocateLocalSlots(") == std::string::npos &&
         vm_text.find("auto setup_frame = [") == std::string::npos;
}

bool VmRuntimeLimitsLiveInRuntimeModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/runtime/runtime_limits.h");
  std::ifstream source("source/VM/src/runtime/runtime_limits.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("bool CheckSequenceLimit(") != std::string::npos &&
         source_text.find("bool CheckSequenceLimit(") != std::string::npos &&
         vm_text.find("bool CheckRuntimeSequenceLimit(") == std::string::npos &&
         vm_text.find("auto check_sequence_limit = [") == std::string::npos &&
         vm_text.find("auto alloc_locals = [") == std::string::npos;
}

bool VmConstantAndGlobalLookupsLiveInInterpreterModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/interpreter/globals.h");
  std::ifstream source("source/VM/src/interpreter/globals.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("bool LoadConstStringSlot(") != std::string::npos &&
         header_text.find("bool IsRefLikeGlobal(") != std::string::npos &&
         source_text.find("bool LoadConstStringSlot(") != std::string::npos &&
         source_text.find("bool IsRefLikeGlobal(") != std::string::npos &&
         vm_text.find("bool LoadConstStringSlot(") == std::string::npos &&
         vm_text.find("bool IsRefLikeGlobal(") == std::string::npos &&
         vm_text.find("auto read_const_string = [") == std::string::npos &&
         vm_text.find("auto is_ref_like_global = [") == std::string::npos;
}

bool VmPrintAnyLivesInRuntimeModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/runtime/print_any.h");
  std::ifstream source("source/VM/src/runtime/print_any.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("bool PrintAny(") != std::string::npos &&
         source_text.find("bool PrintAny(") != std::string::npos &&
         source_text.find("WriteStdoutText(") != std::string::npos &&
         vm_text.find("auto write_text = [") == std::string::npos &&
         vm_text.find("case kPrintAnyTagString:") == std::string::npos;
}

bool VmExecutionStatsLiveInRuntimeModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/runtime/execution_stats.h");
  std::ifstream source("source/VM/src/runtime/execution_stats.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("ExecResult AttachExecutionStats(") != std::string::npos &&
         source_text.find("ExecResult AttachExecutionStats(") != std::string::npos &&
         vm_text.find("ExecResult AttachExecutionStats(") == std::string::npos &&
         vm_text.find("auto finish = [") == std::string::npos;
}

bool VmLlvmJitBackendLivesInJitModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/jit/llvm_backend.h");
  std::ifstream source("source/VM/src/jit/llvm_backend.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("class LlvmJitBackend") != std::string::npos &&
         header_text.find("TryRunFunctionWithRuntime(") != std::string::npos &&
         source_text.find("bool LlvmJitBackend::TryRunFunctionWithRuntime(") != std::string::npos &&
         vm_text.find("class LlvmJitBackend") == std::string::npos;
}

bool VmTrapFormattingLivesInInterpreterModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/interpreter/traps.h");
  std::ifstream source("source/VM/src/interpreter/traps.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return header_text.find("struct TrapContext") != std::string::npos &&
         header_text.find("ExecResult Trap(") != std::string::npos &&
         source_text.find("std::string LookupMethodName(") != std::string::npos &&
         source_text.find("bool ReadU32Operand(") != std::string::npos &&
         source_text.find("bool ReadI32Operand(") != std::string::npos &&
         vm_text.find("std::string LookupMethodName(") == std::string::npos &&
         vm_text.find("bool ReadU32Operand(") == std::string::npos &&
         vm_text.find("bool ReadI32Operand(") == std::string::npos &&
         vm_text.find("auto get_method_name = [") == std::string::npos;
}

bool VmValuePackingHelpersLiveInRuntimeModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/runtime/values.h");
  if (!vm || !header) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  return header_text.find("inline Slot PackI32(") != std::string::npos &&
         header_text.find("inline Slot PackRef(") != std::string::npos &&
         header_text.find("inline float BitsToF32(") != std::string::npos &&
         vm_text.find("inline Slot PackI32(") == std::string::npos &&
         vm_text.find("inline Slot PackRef(") == std::string::npos &&
         vm_text.find("float BitsToF32(") == std::string::npos;
}

bool VmImportDispatcherLivesInRuntimeModule() {
  std::ifstream vm("source/VM/src/vm.cpp");
  std::ifstream header("source/VM/include/runtime/import_dispatch.h");
  std::ifstream source("source/VM/src/runtime/import_dispatch.cpp");
  if (!vm || !header || !source) return false;
  const std::string vm_text((std::istreambuf_iterator<char>(vm)), std::istreambuf_iterator<char>());
  const std::string header_text((std::istreambuf_iterator<char>(header)), std::istreambuf_iterator<char>());
  const std::string source_text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
  return vm_text.find("bool DispatchImportCallByName(") == std::string::npos &&
         vm_text.find("std::string ReadConstPoolString(") == std::string::npos &&
         vm_text.find("auto handle_import_call = [") == std::string::npos &&
         header_text.find("DispatchImportCallByName(") != std::string::npos &&
         source_text.find("bool DispatchImportCallByName(") != std::string::npos &&
         source_text.find("ReadConstPoolString(") != std::string::npos;
}

bool VmRuntimeSplitModulesExist() {
  const std::array<const char*, 20> paths = {
      "source/VM/src/interpreter/interpreter.cpp",
      "source/VM/src/interpreter/dispatch.cpp",
      "source/VM/src/interpreter/frames.cpp",
      "source/VM/src/interpreter/stack.cpp",
      "source/VM/src/native/default_registry.cpp",
      "source/VM/src/native/registry_core.cpp",
      "source/VM/src/native/os.cpp",
      "source/VM/src/native/fs.cpp",
      "source/VM/src/native/path.cpp",
      "source/VM/src/native/env.cpp",
      "source/VM/src/native/time.cpp",
      "source/VM/src/native/random.cpp",
      "source/VM/src/native/log.cpp",
      "source/VM/src/native/channel.cpp",
      "source/VM/src/native/buffer.cpp",
      "source/VM/src/native/json.cpp",
      "source/VM/src/native/thread.cpp",
      "source/VM/src/ffi/dl_runtime.cpp",
      "source/VM/src/jit/jit_scaffold.cpp",
      "source/VM/src/gc/root_tracer.cpp",
  };
  for (const char* path : paths) {
    if (!std::filesystem::exists(path)) return false;
  }
  return std::filesystem::exists("source/VM/src/runtime/runtime_limits.cpp");
}

bool VmBoundaryTypesAreExplicit() {
  const std::array<const char*, 6> paths = {
      "source/VM/include/native/registry.h",
      "source/VM/include/interpreter/frames.h",
      "source/VM/include/interpreter/interpreter.h",
      "source/VM/include/gc/root_tracer.h",
      "source/VM/include/runtime/runtime_limits.h",
      "source/VM/include/ffi/dl_runtime.h",
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
  {"vm_gc_stack_map_collection_lives_in_gc_module", VmGcStackMapCollectionLivesInGcModule},
  {"vm_frame_setup_lives_in_interpreter_module", VmFrameSetupLivesInInterpreterModule},
  {"vm_runtime_limits_live_in_runtime_module", VmRuntimeLimitsLiveInRuntimeModule},
  {"vm_constant_and_global_lookups_live_in_interpreter_module", VmConstantAndGlobalLookupsLiveInInterpreterModule},
  {"vm_print_any_lives_in_runtime_module", VmPrintAnyLivesInRuntimeModule},
  {"vm_execution_stats_live_in_runtime_module", VmExecutionStatsLiveInRuntimeModule},
  {"vm_llvm_jit_backend_lives_in_jit_module", VmLlvmJitBackendLivesInJitModule},
  {"vm_trap_formatting_lives_in_interpreter_module", VmTrapFormattingLivesInInterpreterModule},
  {"vm_value_packing_helpers_live_in_runtime_module", VmValuePackingHelpersLiveInRuntimeModule},
  {"vm_import_dispatcher_lives_in_runtime_module", VmImportDispatcherLivesInRuntimeModule},
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
