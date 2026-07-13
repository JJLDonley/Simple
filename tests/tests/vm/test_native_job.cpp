#include "test_utils.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "native/dispatch.h"
#include "native/registry.h"
#include "runtime/values.h"

namespace Simple::VM::Tests {
namespace {

struct JobTestRuntime {
  Heap heap;
  Native::NativeRegistry natives = Native::BuildDefaultRegistry();
  std::shared_ptr<Runtime::PromiseRegistry> promises =
      std::make_shared<Runtime::PromiseRegistry>();
  Native::NativeResourceRegistry resources;

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

bool VmNativeJobCompletesAndCloses() {
  using Simple::Byte::TypeKind;
  using Runtime::PackI32;
  using Runtime::PackI64;
  using Runtime::UnpackI32;
  using Runtime::UnpackI64;

  JobTestRuntime runtime;
  Native::Slot job = 0;
  bool has_return = false;
  std::string error;
  if (!runtime.Call("System.Job", "spawn", {PackI32(5), PackI64(42)}, TypeKind::I64,
                    &job, &has_return, &error) ||
      !error.empty() || !has_return || job == 0 || runtime.resources.LiveCount() != 1 ||
      runtime.promises->LiveCount() != 1) {
    return false;
  }

  Native::Slot result = 0;
  if (!runtime.Call("System.Job", "await", {job}, TypeKind::I64, &result,
                    &has_return, &error) ||
      !error.empty() || UnpackI64(result) != 42) {
    return false;
  }
  Native::Slot state = 0;
  if (!runtime.Call("System.Job", "poll", {job}, TypeKind::I32, &state,
                    &has_return, &error) ||
      !error.empty() || UnpackI32(state) != 1) {
    return false;
  }
  Native::Slot ignored = 0;
  return runtime.Call("System.Job", "close", {job}, TypeKind::Unspecified, &ignored,
                      &has_return, &error) &&
         error.empty() && !has_return && runtime.resources.LiveCount() == 0 &&
         runtime.promises->LiveCount() == 0;
}

bool VmNativePromiseReportsFailureAndCancellation() {
  using Simple::Byte::TypeKind;
  using Runtime::PackI32;
  using Runtime::PackRef;
  using Runtime::UnpackI32;

  JobTestRuntime runtime;
  bool has_return = false;
  std::string error;
  const uint32_t message = CreateString(runtime.heap, AsciiToU16("job failed"));
  Native::Slot failed = 0;
  if (!runtime.Call("System.Job", "runFailed", {PackI32(0), PackRef(message)},
                    TypeKind::I64, &failed, &has_return, &error) ||
      !error.empty() || failed == 0) {
    return false;
  }
  Native::Slot awaited = 1;
  if (!runtime.Call("System.Job", "await", {failed}, TypeKind::I64, &awaited,
                    &has_return, &error) ||
      !error.empty()) {
    return false;
  }
  Native::Slot is_failed = 0;
  if (!runtime.Call("System.Job", "isFailed", {failed}, TypeKind::Bool,
                    &is_failed, &has_return, &error) ||
      !error.empty() || UnpackI32(is_failed) != 1) {
    return false;
  }
  Native::Slot error_ref = 0;
  if (!runtime.Call("System.Job", "error", {failed}, TypeKind::String,
                    &error_ref, &has_return, &error) ||
      !error.empty()) {
    return false;
  }
  const HeapObject* error_object = runtime.heap.Get(Runtime::UnpackRef(error_ref));
  if (!error_object || error_object->header.kind != ObjectKind::String ||
      U16ToAscii(ReadString(error_object)) != "job failed") {
    return false;
  }
  Native::Slot ignored = 0;
  if (!runtime.Call("System.Job", "close", {failed}, TypeKind::Unspecified,
                    &ignored, &has_return, &error) ||
      !error.empty()) {
    return false;
  }

  Native::Slot pending = 0;
  if (!runtime.Call("System.Job", "run", {PackI32(10000), Runtime::PackI64(9)},
                    TypeKind::I64, &pending, &has_return, &error) ||
      !error.empty() || pending == 0) {
    return false;
  }
  Native::Slot canceled = 0;
  if (!runtime.Call("System.Job", "cancel", {pending}, TypeKind::Bool,
                    &canceled, &has_return, &error) ||
      !error.empty() || UnpackI32(canceled) != 1) {
    return false;
  }
  Native::Slot is_canceled = 0;
  if (!runtime.Call("System.Job", "isCancelled", {pending}, TypeKind::Bool,
                    &is_canceled, &has_return, &error) ||
      !error.empty() || UnpackI32(is_canceled) != 1) {
    return false;
  }
  return runtime.Call("System.Job", "close", {pending}, TypeKind::Unspecified,
                      &ignored, &has_return, &error) &&
         error.empty() && runtime.resources.LiveCount() == 0 &&
         runtime.promises->LiveCount() == 0;
}

bool VmNativeJobShutdownCancelsWithoutDelay() {
  using Simple::Byte::TypeKind;
  auto runtime = std::make_unique<JobTestRuntime>();
  Native::Slot job = 0;
  bool has_return = false;
  std::string error;
  if (!runtime->Call("System.Job", "spawn",
                     {Runtime::PackI32(60000), Runtime::PackI64(1)}, TypeKind::I64,
                     &job, &has_return, &error) ||
      !error.empty() || job == 0) {
    return false;
  }
  // Exclude sanitizer/runtime first-thread initialization from the shutdown timing.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto start = std::chrono::steady_clock::now();
  runtime.reset();
  return std::chrono::steady_clock::now() - start < std::chrono::seconds(2);
}

bool VmNativeJobMetadataDefinesAsyncBoundaries() {
  using Native::NativeAllocationBehavior;
  using Native::NativeBlockingBehavior;
  using Native::NativeGcBehavior;
  using Native::NativeLayer;
  using Native::NativeResourceAccess;
  using Native::NativeResourceKind;

  const Native::NativeRegistry registry = Native::BuildDefaultRegistry();
  const auto* spawn = registry.Find("System.Job", "spawn");
  const auto* await = registry.Find("System.Job", "await");
  const auto* run = registry.Find("System.Job", "run");
  const auto* poll = registry.Find("System.Job", "poll");
  return spawn && await && run && poll && spawn->resources.size() == 1 &&
         spawn->resources[0].kind == NativeResourceKind::Job &&
         spawn->resources[0].access == NativeResourceAccess::Output &&
         spawn->allocation == NativeAllocationBehavior::MayAllocateHost &&
         spawn->gc_behavior == NativeGcBehavior::MaySafepoint &&
         await->blocking == NativeBlockingBehavior::MayBlock &&
         await->resources[0].access == NativeResourceAccess::Input &&
         run->layer == NativeLayer::Standard &&
         run->allocation == NativeAllocationBehavior::MayAllocateHost &&
         poll->blocking == NativeBlockingBehavior::NonBlocking &&
         poll->gc_behavior == NativeGcBehavior::MaySafepoint;
}

const TestCase kVmNativeJobTests[] = {
    {"vm_native_job_completes_and_closes", VmNativeJobCompletesAndCloses},
    {"vm_native_promise_reports_failure_and_cancellation",
     VmNativePromiseReportsFailureAndCancellation},
    {"vm_native_job_shutdown_cancels_without_delay", VmNativeJobShutdownCancelsWithoutDelay},
    {"vm_native_job_metadata_defines_async_boundaries",
     VmNativeJobMetadataDefinesAsyncBoundaries},
};

const TestSection kVmNativeJobSections[] = {
    {"vm_native_job", kVmNativeJobTests,
     sizeof(kVmNativeJobTests) / sizeof(kVmNativeJobTests[0])},
};

} // namespace

const TestSection* GetVmNativeJobSections(size_t* count) {
  if (count) *count = sizeof(kVmNativeJobSections) / sizeof(kVmNativeJobSections[0]);
  return kVmNativeJobSections;
}

} // namespace Simple::VM::Tests
