#include "native/registry.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "native/arg_utils.h"
#include "native/job.h"
#include "native/spec_builder.h"
#include "platform/platform.h"

namespace Simple::VM::Native {
namespace {

struct ProcessResource {
  std::shared_ptr<Simple::Platform::Process> process;
};

bool CloseProcessResource(void* payload, std::string* error) {
  auto* resource = static_cast<ProcessResource*>(payload);
  if (!resource || !resource->process) return true;
  const bool closed = resource->process->Close(error);
  resource->process.reset();
  return closed;
}

ProcessResource* GetProcessResource(NativeCallContext& context) {
  NativeResourceRecord* record = nullptr;
  if (context.ArgResourceHandle(0, NativeResourceKind::Process, nullptr, &record) !=
          NativeResourceStatus::Ok ||
      !record || !record->payload) {
    return nullptr;
  }
  return static_cast<ProcessResource*>(record->payload.get());
}

bool ReadProcessRequest(NativeCallContext& context,
                        Simple::Platform::ProcessStartRequest* request) {
  return request && ReadStringArg(context, 0, &request->program) &&
         ReadStringSequence(context, 1, &request->arguments);
}

NativeCallResult ProcessSpawn(NativeCallContext& context) {
  if (!context.resource_registry) return NativeCallResult::Error("process registry unavailable");
  Simple::Platform::ProcessStartRequest request;
  if (!ReadProcessRequest(context, &request)) {
    return NativeCallResult::Error("System.Process.spawn expects a program and string arguments");
  }
  std::string error;
  auto process = Simple::Platform::SpawnProcess(request, &error);
  if (!process) return NativeCallResult::Error(std::move(error));

  auto resource = std::make_shared<ProcessResource>();
  resource->process = std::move(process);
  NativeResourceRecord record;
  record.kind = NativeResourceKind::Process;
  record.debug_label = "System.Process";
  record.payload = resource;
  record.close = CloseProcessResource;
  const NativeHandleId handle = context.resource_registry->Insert(std::move(record));
  if (handle.IsNull()) {
    (void)resource->process->Close(nullptr);
    return NativeCallResult::Error("System.Process resource allocation failed");
  }
  return NativeCallResult::Handle(handle);
}

NativeCallResult ProcessWait(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  if (!resource || !resource->process) return NativeCallResult::Error("invalid process handle");
  (void)resource->process->CloseStdin(nullptr);
  int32_t exit_code = -1;
  std::string error;
  if (!resource->process->Wait(&exit_code, &error)) {
    return NativeCallResult::Error(std::move(error));
  }
  return NativeCallResult::I32(exit_code);
}

NativeCallResult ProcessKill(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  if (!resource || !resource->process) return NativeCallResult::Bool(false);
  std::string error;
  if (!resource->process->Kill(&error)) return NativeCallResult::Error(std::move(error));
  return NativeCallResult::Bool(true);
}

NativeCallResult ProcessExitCode(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  if (!resource || !resource->process) return NativeCallResult::I32(-1);
  int32_t exit_code = -1;
  bool exited = false;
  std::string error;
  if (!resource->process->Poll(&exit_code, &exited, &error)) {
    return NativeCallResult::Error(std::move(error));
  }
  return NativeCallResult::I32(exited ? exit_code : -1);
}

NativeCallResult ProcessStdin(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  std::string text;
  if (!resource || !resource->process || !ReadStringArg(context, 1, &text)) {
    return NativeCallResult::Bool(false);
  }
  std::string error;
  if (!resource->process->WriteStdin(text, &error)) {
    return NativeCallResult::Error(std::move(error));
  }
  return NativeCallResult::Bool(true);
}

NativeCallResult ProcessCloseStdin(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  if (!resource || !resource->process) return NativeCallResult::Error("invalid process handle");
  std::string error;
  if (!resource->process->CloseStdin(&error)) return NativeCallResult::Error(std::move(error));
  return NativeCallResult::Void();
}

NativeCallResult ProcessStdout(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  return resource && resource->process ? NativeCallResult::String(resource->process->Stdout())
                                       : NativeCallResult::String({});
}

NativeCallResult ProcessStderr(NativeCallContext& context) {
  ProcessResource* resource = GetProcessResource(context);
  return resource && resource->process ? NativeCallResult::String(resource->process->Stderr())
                                       : NativeCallResult::String({});
}

NativeCallResult ProcessClose(NativeCallContext& context) {
  NativeHandleId handle;
  if (!context.resource_registry || !context.ArgHandle(0, &handle)) {
    return NativeCallResult::Error("invalid process handle");
  }
  std::string error;
  const NativeResourceStatus status =
      context.resource_registry->Close(handle, NativeResourceKind::Process, &error);
  if (status != NativeResourceStatus::Ok) {
    return NativeCallResult::Error(error.empty() ? "invalid process handle" : std::move(error));
  }
  return NativeCallResult::Void();
}

bool RunProcess(NativeCallContext& context,
                std::shared_ptr<Simple::Platform::Process>* process,
                int32_t* exit_code,
                std::string* error) {
  Simple::Platform::ProcessStartRequest request;
  if (!ReadProcessRequest(context, &request)) {
    if (error) *error = "Standard.Process expects a program and string arguments";
    return false;
  }
  *process = Simple::Platform::SpawnProcess(request, error);
  if (!*process) return false;
  (void)(*process)->CloseStdin(nullptr);
  return (*process)->Wait(exit_code, error);
}

NativeCallResult StandardProcessRun(NativeCallContext& context) {
  std::shared_ptr<Simple::Platform::Process> process;
  int32_t exit_code = -1;
  std::string error;
  return RunProcess(context, &process, &exit_code, &error)
             ? NativeCallResult::I32(exit_code)
             : NativeCallResult::Error(std::move(error));
}

NativeCallResult StandardProcessRunText(NativeCallContext& context) {
  std::shared_ptr<Simple::Platform::Process> process;
  int32_t exit_code = -1;
  std::string error;
  if (!RunProcess(context, &process, &exit_code, &error)) {
    return NativeCallResult::Error(std::move(error));
  }
  return NativeCallResult::String(process->Stdout());
}

NativeCallResult StandardProcessRunBytes(NativeCallContext& context) {
  std::shared_ptr<Simple::Platform::Process> process;
  int32_t exit_code = -1;
  std::string error;
  if (!RunProcess(context, &process, &exit_code, &error)) {
    return NativeCallResult::Error(std::move(error));
  }
  if (!context.heap) return NativeCallResult::Error("VM heap unavailable");
  std::vector<uint32_t> bytes;
  const std::string output = process->Stdout();
  bytes.reserve(output.size());
  for (unsigned char byte : output) bytes.push_back(byte);
  NativeCallResult result;
  result.value = CreateByteList(*context.heap, bytes);
  return result;
}

NativeCallResult StandardProcessRunAsync(NativeCallContext& context) {
  Simple::Platform::ProcessStartRequest request;
  if (!ReadProcessRequest(context, &request)) {
    return NativeCallResult::Error("Standard.Process.runAsync expects a program and string arguments");
  }
  return StartNativeJob(
      context, [request = std::move(request)](NativeJobControl& control) mutable {
        std::string error;
        auto process = Simple::Platform::SpawnProcess(request, &error);
        if (!process) return NativeJobResult::Failure(std::move(error));
        (void)process->CloseStdin(nullptr);
        int32_t exit_code = -1;
        for (;;) {
          bool exited = false;
          if (!process->Poll(&exit_code, &exited, &error)) {
            return NativeJobResult::Failure(std::move(error));
          }
          if (exited) return NativeJobResult::Success(exit_code);
          if (control.WaitFor(std::chrono::milliseconds(10))) {
            (void)process->Kill(nullptr);
            (void)process->Wait(&exit_code, nullptr);
            return NativeJobResult::Success(exit_code);
          }
        }
      });
}

NativeFunctionSpec ProcessSpec(Simple::Lang::SystemProcessMember member,
                               std::vector<Simple::Byte::TypeKind> params,
                               Simple::Byte::TypeKind result,
                               NativeFunctionHandler handler,
                               NativeResourceAccess access,
                               const char* capability) {
  return WithCapability(
      WithResource(MaySafepoint(MayAllocateHost(MayBlock(MakeSpec(
                       Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Process),
                       Simple::Lang::ToMember(member), std::move(params), result,
                       std::move(handler))))),
                   NativeResourceKind::Process, access,
                   access == NativeResourceAccess::Output ? kNativeResourceNoParameter : 0),
      capability);
}

NativeFunctionSpec StandardProcessSpec(Simple::Lang::StandardProcessMember member,
                                       Simple::Byte::TypeKind result,
                                       NativeFunctionHandler handler) {
  return AsStandardModule(
      WithCapability(MaySafepoint(MayAllocateHost(MayBlock(MakeSpec(
                         Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Process),
                         Simple::Lang::ToMember(member),
                         {Simple::Byte::TypeKind::String, Simple::Byte::TypeKind::Ref},
                         result, std::move(handler))))),
                     "process.spawn"),
      Simple::Lang::StandardModule::Process);
}

} // namespace

void RegisterSystemProcessFunctions(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  using Simple::Lang::SystemProcessMember;
  registry.Register(ProcessSpec(SystemProcessMember::Spawn, {TypeKind::String, TypeKind::Ref},
                                TypeKind::I64, ProcessSpawn, NativeResourceAccess::Output,
                                "process.spawn"));
  registry.Register(ProcessSpec(SystemProcessMember::Wait, {TypeKind::I64}, TypeKind::I32,
                                ProcessWait, NativeResourceAccess::InputOutput,
                                "process.wait"));
  registry.Register(ProcessSpec(SystemProcessMember::Kill, {TypeKind::I64}, TypeKind::Bool,
                                ProcessKill, NativeResourceAccess::InputOutput,
                                "process.kill"));
  registry.Register(ProcessSpec(SystemProcessMember::ExitCode, {TypeKind::I64}, TypeKind::I32,
                                ProcessExitCode, NativeResourceAccess::Input,
                                "process.wait"));
  registry.Register(ProcessSpec(SystemProcessMember::Stdin, {TypeKind::I64, TypeKind::String},
                                TypeKind::Bool, ProcessStdin,
                                NativeResourceAccess::InputOutput, "process.stdio"));
  registry.Register(ProcessSpec(SystemProcessMember::CloseStdin, {TypeKind::I64},
                                TypeKind::Unspecified, ProcessCloseStdin,
                                NativeResourceAccess::InputOutput, "process.stdio"));
  registry.Register(ProcessSpec(SystemProcessMember::Stdout, {TypeKind::I64}, TypeKind::String,
                                ProcessStdout, NativeResourceAccess::Input,
                                "process.stdio"));
  registry.Register(ProcessSpec(SystemProcessMember::Stderr, {TypeKind::I64}, TypeKind::String,
                                ProcessStderr, NativeResourceAccess::Input,
                                "process.stdio"));
  registry.Register(ProcessSpec(SystemProcessMember::Close, {TypeKind::I64},
                                TypeKind::Unspecified, ProcessClose, NativeResourceAccess::InputOutput,
                                "process.close"));

  registry.Register(StandardProcessSpec(Simple::Lang::StandardProcessMember::Run,
                                        TypeKind::I32, StandardProcessRun));
  registry.Register(StandardProcessSpec(Simple::Lang::StandardProcessMember::RunText,
                                        TypeKind::String, StandardProcessRunText));
  registry.Register(StandardProcessSpec(Simple::Lang::StandardProcessMember::RunBytes,
                                        TypeKind::Ref, StandardProcessRunBytes));
  registry.Register(MaySafepoint(WithResource(
      StandardProcessSpec(Simple::Lang::StandardProcessMember::RunAsync,
                          TypeKind::I64, StandardProcessRunAsync),
      NativeResourceKind::Job, NativeResourceAccess::Output)));
}

} // namespace Simple::VM::Native
