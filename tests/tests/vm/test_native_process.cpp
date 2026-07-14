#include "test_utils.h"

#include <memory>
#include <string>
#include <vector>

#include "heap.h"
#include "native/arg_utils.h"
#include "native/dispatch.h"
#include "native/registry.h"
#include "runtime/values.h"

namespace Simple::VM::Tests {
namespace {

struct ProcessTestRuntime {
  Heap heap;
  Native::NativeRegistry natives = Native::BuildDefaultRegistry();
  std::shared_ptr<Runtime::PromiseRegistry> promises =
      std::make_shared<Runtime::PromiseRegistry>();
  Native::NativeResourceRegistry resources;

  Native::Slot String(const std::string& value) {
    return Runtime::PackRef(CreateString(heap, AsciiToU16(value)));
  }

  Native::Slot Strings(const std::vector<std::string>& values) {
    std::vector<uint32_t> string_handles;
    string_handles.reserve(values.size());
    for (const auto& value : values) {
      string_handles.push_back(CreateString(heap, AsciiToU16(value)));
    }
    const uint32_t handle =
        heap.Allocate(ObjectKind::List, 0, 8u + static_cast<uint32_t>(values.size()) * 4u);
    HeapObject* object = heap.Get(handle);
    if (!object) return Runtime::PackRef(HeapLayout::kNullRef);
    Native::WriteU32(object->payload, 0, static_cast<uint32_t>(values.size()));
    Native::WriteU32(object->payload, 4, static_cast<uint32_t>(values.size()));
    for (size_t i = 0; i < string_handles.size(); ++i) {
      Native::WriteU32(object->payload, 8u + i * 4u, string_handles[i]);
    }
    return Runtime::PackRef(handle);
  }

  std::string ReadString(Native::Slot value) {
    const HeapObject* object = heap.Get(Runtime::UnpackRef(value));
    if (!object || object->header.kind != ObjectKind::String || object->payload.size() < 4) {
      return {};
    }
    const uint32_t length = Native::ReadU32(object->payload, 0);
    std::string result;
    result.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
      const size_t offset = 4u + i * 2u;
      if (offset + 1 >= object->payload.size()) return {};
      const uint16_t character = object->payload[offset] |
                                 (static_cast<uint16_t>(object->payload[offset + 1]) << 8u);
      result.push_back(static_cast<char>(character));
    }
    return result;
  }

  std::string ReadByteList(Native::Slot value) {
    const HeapObject* object = heap.Get(Runtime::UnpackRef(value));
    if (!object || object->header.kind != ObjectKind::List || object->payload.size() < 8) {
      return {};
    }
    const uint32_t length = Native::ReadU32(object->payload, 0);
    if (8u + static_cast<size_t>(length) * 4u > object->payload.size()) return {};
    std::string result;
    result.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
      result.push_back(static_cast<char>(Native::ReadU32(object->payload, 8u + i * 4u)));
    }
    return result;
  }

  bool Call(const std::string& module,
            const std::string& symbol,
            const std::vector<Native::Slot>& args,
            Simple::Byte::TypeKind return_kind,
            Native::Slot* out,
            bool* has_return,
            std::string* error) {
    Native::MetadataDispatchContext context;
    context.heap = &heap;
    context.resource_registry = &resources;
    context.promise_registry = promises;
    return Native::DispatchMetadataImport(natives, module, symbol, args, return_kind,
                                          context, out, has_return, error);
  }
};

bool Call(ProcessTestRuntime* runtime,
          const std::string& symbol,
          const std::vector<Native::Slot>& args,
          Simple::Byte::TypeKind result_kind,
          Native::Slot* result,
          std::string* error) {
  bool has_return = false;
  return runtime->Call("System.Process", symbol, args, result_kind, result, &has_return,
                       error) &&
         (result_kind == Simple::Byte::TypeKind::Void || has_return);
}

Native::Slot Spawn(ProcessTestRuntime* runtime,
                   const std::vector<std::string>& arguments,
                   std::string* error) {
  Native::Slot process = 0;
  if (!Call(runtime, "spawn",
            {runtime->String(SIMPLEVM_PROCESS_FIXTURE), runtime->Strings(arguments)},
            Simple::Byte::TypeKind::I64, &process, error)) {
    return 0;
  }
  return process;
}

bool VmNativeProcessCapturesOutputAndExitStatus() {
  using Simple::Byte::TypeKind;
  ProcessTestRuntime runtime;
  std::string error;
  const Native::Slot process = Spawn(&runtime, {"emit", "hello", "problem", "7"}, &error);
  Native::Slot result = 0;
  if (!error.empty() || process == 0 || runtime.resources.LiveCount() != 1 ||
      !Call(&runtime, "wait", {process}, TypeKind::I32, &result, &error) ||
      Runtime::UnpackI32(result) != 7) {
    return false;
  }
  Native::Slot output = 0;
  Native::Slot diagnostic = 0;
  Native::Slot exit_code = 0;
  if (!Call(&runtime, "stdout", {process}, TypeKind::String, &output, &error) ||
      !Call(&runtime, "stderr", {process}, TypeKind::String, &diagnostic, &error) ||
      !Call(&runtime, "exitCode", {process}, TypeKind::I32, &exit_code, &error) ||
      runtime.ReadString(output) != "hello" || runtime.ReadString(diagnostic) != "problem" ||
      Runtime::UnpackI32(exit_code) != 7) {
    return false;
  }
  return Call(&runtime, "close", {process}, TypeKind::Void, &result, &error) &&
         error.empty() && runtime.resources.LiveCount() == 0;
}

bool VmNativeProcessSupportsStdinAndKill() {
  using Simple::Byte::TypeKind;
  ProcessTestRuntime runtime;
  std::string error;
  Native::Slot result = 0;
  const Native::Slot input_process = Spawn(&runtime, {"stdin"}, &error);
  if (input_process == 0 ||
      !Call(&runtime, "stdin", {input_process, runtime.String("from-parent")},
            TypeKind::Bool, &result, &error) ||
      Runtime::UnpackI32(result) != 1 ||
      !Call(&runtime, "closeStdin", {input_process}, TypeKind::Void, &result, &error) ||
      !Call(&runtime, "wait", {input_process}, TypeKind::I32, &result, &error)) {
    return false;
  }
  Native::Slot output = 0;
  if (!Call(&runtime, "stdout", {input_process}, TypeKind::String, &output, &error) ||
      runtime.ReadString(output) != "from-parent" ||
      !Call(&runtime, "close", {input_process}, TypeKind::Void, &result, &error)) {
    return false;
  }

  const Native::Slot sleeping_process = Spawn(&runtime, {"sleep"}, &error);
  if (sleeping_process == 0 ||
      !Call(&runtime, "kill", {sleeping_process}, TypeKind::Bool, &result, &error) ||
      Runtime::UnpackI32(result) != 1 ||
      !Call(&runtime, "wait", {sleeping_process}, TypeKind::I32, &result, &error) ||
      Runtime::UnpackI32(result) == 0) {
    return false;
  }
  return Call(&runtime, "close", {sleeping_process}, TypeKind::Void, &result, &error) &&
         error.empty() && runtime.resources.LiveCount() == 0;
}

bool VmStandardProcessRunsTextAndAsync() {
  using Simple::Byte::TypeKind;
  ProcessTestRuntime runtime;
  std::string error;
  bool has_return = false;
  Native::Slot result = 0;
  if (!runtime.Call("System.Process", "runText",
                    {runtime.String(SIMPLEVM_PROCESS_FIXTURE),
                     runtime.Strings({"emit", "standard", "", "0"})},
                    TypeKind::String, &result, &has_return, &error) ||
      !error.empty() || runtime.ReadString(result) != "standard") {
    return false;
  }
  if (!runtime.Call("System.Process", "runBytes",
                    {runtime.String(SIMPLEVM_PROCESS_FIXTURE),
                     runtime.Strings({"emit", "bytes", "", "0"})},
                    TypeKind::Ref, &result, &has_return, &error) ||
      !error.empty() || runtime.ReadByteList(result) != "bytes") {
    return false;
  }
  if (!runtime.Call("System.Process", "runAsync",
                    {runtime.String(SIMPLEVM_PROCESS_FIXTURE),
                     runtime.Strings({"emit", "", "", "9"})},
                    TypeKind::I64, &result, &has_return, &error) ||
      !error.empty() || result == 0) {
    return false;
  }
  const Native::Slot job = result;
  if (!runtime.Call("System.Job", "await", {job}, TypeKind::I64, &result, &has_return,
                    &error) ||
      !error.empty() || Runtime::UnpackI64(result) != 9 ||
      !runtime.Call("System.Job", "close", {job}, TypeKind::Void, &result, &has_return,
                    &error)) {
    return false;
  }
  return error.empty() && runtime.resources.LiveCount() == 0 && runtime.promises->LiveCount() == 0;
}

bool VmNativeProcessMetadataDefinesCapabilities() {
  const Native::NativeRegistry registry = Native::BuildDefaultRegistry();
  const Native::NativeFunctionSpec* spawn = registry.Find("System.Process", "spawn");
  const Native::NativeFunctionSpec* wait = registry.Find("System.Process", "wait");
  const Native::NativeFunctionSpec* run_async = registry.Find("System.Process", "runAsync");
  return spawn && wait && run_async && !spawn->capability_tags.empty() &&
         spawn->capability_tags[0] == "process.spawn" && !wait->capability_tags.empty() &&
         wait->capability_tags[0] == "process.wait" && spawn->resources.size() == 1 &&
         spawn->resources[0].kind == Native::NativeResourceKind::Process &&
         spawn->resources[0].access == Native::NativeResourceAccess::Output &&
         wait->blocking == Native::NativeBlockingBehavior::MayBlock &&
         run_async->layer == Native::NativeLayer::Standard;
}

const TestCase kVmNativeProcessTests[] = {
    {"vm_native_process_captures_output_and_exit_status",
     VmNativeProcessCapturesOutputAndExitStatus},
    {"vm_native_process_supports_stdin_and_kill", VmNativeProcessSupportsStdinAndKill},
    {"vm_standard_process_runs_text_and_async", VmStandardProcessRunsTextAndAsync},
    {"vm_native_process_metadata_defines_capabilities",
     VmNativeProcessMetadataDefinesCapabilities},
};

const TestSection kVmNativeProcessSections[] = {
    {"vm_native_process", kVmNativeProcessTests,
     sizeof(kVmNativeProcessTests) / sizeof(kVmNativeProcessTests[0])},
};

} // namespace

const TestSection* GetVmNativeProcessSections(size_t* count) {
  if (count) {
    *count = sizeof(kVmNativeProcessSections) / sizeof(kVmNativeProcessSections[0]);
  }
  return kVmNativeProcessSections;
}

} // namespace Simple::VM::Tests
