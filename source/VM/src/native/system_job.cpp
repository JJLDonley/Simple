#include "native/registry.h"
#include "runtime/values.h"

#include "native/arg_utils.h"
#include "native/spec_builder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace Simple::VM::Native {
namespace {

using Simple::VM::Runtime::AbiPromiseId;
using Simple::VM::Runtime::PromiseRecord;
using Simple::VM::Runtime::PromiseRegistry;
using Simple::VM::Runtime::PromiseState;
using Simple::VM::Runtime::PromiseStatus;

constexpr int32_t kMaximumJobDelayMs = 24 * 60 * 60 * 1000;

enum class JobCompletion {
  Resolve,
  Fail,
};

struct JobResource {
  std::shared_ptr<PromiseRegistry> promises;
  AbiPromiseId promise;
  std::atomic<bool> cancellation_requested{false};
  std::atomic<bool> released{false};
  std::mutex wait_mutex;
  std::condition_variable wake;
  std::thread worker;

  ~JobResource() { Shutdown(); }

  bool RequestCancel() {
    cancellation_requested.store(true, std::memory_order_release);
    wake.notify_all();
    return promises && promises->Cancel(promise) == PromiseStatus::Ok;
  }

  void Shutdown() {
    (void)RequestCancel();
    if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
    if (promises && !released.exchange(true, std::memory_order_acq_rel)) {
      (void)promises->Release(promise);
    }
  }
};

bool CloseJobResource(void* payload, std::string*) {
  auto* job = static_cast<JobResource*>(payload);
  if (job) job->Shutdown();
  return true;
}

JobResource* GetJobResource(NativeCallContext& context) {
  NativeResourceRecord* record = nullptr;
  if (context.ArgResourceHandle(0, NativeResourceKind::Job, nullptr, &record) !=
          NativeResourceStatus::Ok ||
      !record || !record->payload) {
    return nullptr;
  }
  return static_cast<JobResource*>(record->payload.get());
}

int32_t PromiseStateCode(PromiseState state) {
  switch (state) {
    case PromiseState::Pending:
      return 0;
    case PromiseState::Done:
      return 1;
    case PromiseState::Failed:
      return 2;
    case PromiseState::Canceled:
      return 3;
  }
  return -1;
}

NativeCallResult StartJob(NativeCallContext& context,
                          int32_t delay_ms,
                          JobCompletion completion,
                          int64_t result,
                          std::string error) {
  if (!context.resource_registry || !context.promise_registry || delay_ms < 0 ||
      delay_ms > kMaximumJobDelayMs) {
    return NativeCallResult::Handle({});
  }

  auto job = std::make_shared<JobResource>();
  job->promises = context.promise_registry;
  job->promise = job->promises->Create();

  NativeResourceRecord record;
  record.kind = NativeResourceKind::Job;
  record.debug_label = "System.Job";
  record.payload = job;
  record.close = CloseJobResource;
  const NativeHandleId handle = context.resource_registry->Insert(std::move(record));
  if (handle.IsNull()) {
    (void)job->RequestCancel();
    (void)job->promises->Release(job->promise);
    job->released.store(true, std::memory_order_release);
    return NativeCallResult::Handle({});
  }

  try {
    job->worker = std::thread([job, delay_ms, completion, result,
                               error = std::move(error)]() mutable {
      std::unique_lock<std::mutex> lock(job->wait_mutex);
      const bool canceled = job->wake.wait_for(
          lock, std::chrono::milliseconds(delay_ms), [&] {
            return job->cancellation_requested.load(std::memory_order_acquire);
          });
      lock.unlock();
      if (canceled) {
        (void)job->promises->Cancel(job->promise);
      } else if (completion == JobCompletion::Fail) {
        (void)job->promises->Fail(job->promise, std::move(error));
      } else {
        (void)job->promises->Resolve(job->promise, static_cast<uint64_t>(result));
      }
    });
  } catch (const std::exception& exception) {
    std::string ignored;
    (void)context.resource_registry->Close(handle, NativeResourceKind::Job, &ignored);
    return NativeCallResult::Error(std::string("System.Job.spawn failed: ") + exception.what());
  }

  return NativeCallResult::Handle(handle);
}

NativeCallResult JobSpawn(NativeCallContext& context) {
  int32_t delay_ms = 0;
  int64_t result = 0;
  if (!context.ArgI32(0, &delay_ms) || !context.ArgI64(1, &result)) {
    return NativeCallResult::Handle({});
  }
  return StartJob(context, delay_ms, JobCompletion::Resolve, result, {});
}

NativeCallResult JobSpawnFailed(NativeCallContext& context) {
  int32_t delay_ms = 0;
  std::string error;
  if (!context.ArgI32(0, &delay_ms) || !context.ArgString(1, &error)) {
    return NativeCallResult::Handle({});
  }
  return StartJob(context, delay_ms, JobCompletion::Fail, 0, std::move(error));
}

NativeCallResult JobCancel(NativeCallContext& context) {
  JobResource* job = GetJobResource(context);
  if (!job || !job->promises) return NativeCallResult::Bool(false);
  return NativeCallResult::Bool(job->RequestCancel());
}

NativeCallResult JobPoll(NativeCallContext& context) {
  JobResource* job = GetJobResource(context);
  PromiseRecord record;
  if (!job || !job->promises ||
      job->promises->Get(job->promise, &record) != PromiseStatus::Ok) {
    return NativeCallResult::I32(-1);
  }
  return NativeCallResult::I32(PromiseStateCode(record.state));
}

NativeCallResult JobAwait(NativeCallContext& context) {
  JobResource* job = GetJobResource(context);
  PromiseRecord record;
  if (!job || !job->promises ||
      job->promises->Wait(job->promise, &record) != PromiseStatus::Ok ||
      record.state != PromiseState::Done || record.payload_is_ref) {
    return NativeCallResult::I64(0);
  }
  return NativeCallResult::I64(static_cast<int64_t>(record.payload));
}

NativeCallResult JobError(NativeCallContext& context) {
  JobResource* job = GetJobResource(context);
  PromiseRecord record;
  if (!job || !job->promises ||
      job->promises->Get(job->promise, &record) != PromiseStatus::Ok) {
    return NativeCallResult::String("invalid job");
  }
  return NativeCallResult::String(record.error);
}

NativeCallResult JobIsState(NativeCallContext& context, PromiseState expected) {
  JobResource* job = GetJobResource(context);
  PromiseRecord record;
  return NativeCallResult::Bool(
      job && job->promises &&
      job->promises->Get(job->promise, &record) == PromiseStatus::Ok &&
      record.state == expected);
}

NativeCallResult JobIsDone(NativeCallContext& context) {
  JobResource* job = GetJobResource(context);
  PromiseRecord record;
  return NativeCallResult::Bool(
      job && job->promises &&
      job->promises->Get(job->promise, &record) == PromiseStatus::Ok &&
      record.state != PromiseState::Pending);
}

NativeCallResult JobIsFailed(NativeCallContext& context) {
  return JobIsState(context, PromiseState::Failed);
}

NativeCallResult JobIsCancelled(NativeCallContext& context) {
  return JobIsState(context, PromiseState::Canceled);
}

NativeCallResult JobClose(NativeCallContext& context) {
  if (!context.resource_registry) {
    return NativeCallResult::Error("System.Job.close resource registry unavailable");
  }
  NativeHandleId handle;
  if (!context.ArgHandle(0, &handle)) {
    return NativeCallResult::Error("System.Job.close invalid handle encoding");
  }
  const NativeResourceStatus status =
      context.resource_registry->Close(handle, NativeResourceKind::Job, nullptr);
  if (status != NativeResourceStatus::Ok) {
    return NativeCallResult::Error("System.Job.close invalid resource handle: " +
                                   std::string(NativeResourceStatusName(status)));
  }
  return NativeCallResult::Void();
}

NativeFunctionSpec JobResourceSpec(NativeFunctionSpec spec,
                                   NativeResourceAccess access,
                                   uint32_t parameter_index = kNativeResourceNoParameter) {
  return MaySafepoint(WithResource(std::move(spec), NativeResourceKind::Job, access,
                                   parameter_index));
}

NativeFunctionSpec StandardSpec(Simple::Lang::StandardPromiseMember member,
                                std::vector<Simple::Byte::TypeKind> params,
                                Simple::Byte::TypeKind result,
                                NativeFunctionHandler handler) {
  return AsStandardModule(
      MakeSpec(Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Job),
               Simple::Lang::ToMember(member), std::move(params), result,
               std::move(handler)),
      Simple::Lang::StandardModule::Promise);
}

void RegisterPromiseSurface(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  using Simple::Lang::StandardPromiseMember;
  registry.Register(JobResourceSpec(
      WithCapability(MayAllocateHost(StandardSpec(
                         StandardPromiseMember::Run, {TypeKind::I32, TypeKind::I64},
                         TypeKind::I64, JobSpawn)),
                     "threading"),
      NativeResourceAccess::Output));
  registry.Register(JobResourceSpec(
      WithCapability(MayAllocateHost(StandardSpec(
                         StandardPromiseMember::RunFailed,
                         {TypeKind::I32, TypeKind::String}, TypeKind::I64,
                         JobSpawnFailed)),
                     "threading"),
      NativeResourceAccess::Output));
  registry.Register(JobResourceSpec(
      MayBlock(StandardSpec(StandardPromiseMember::Await, {TypeKind::I64}, TypeKind::I64,
                            JobAwait)),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::Poll, {TypeKind::I64}, TypeKind::I32, JobPoll),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::Cancel, {TypeKind::I64}, TypeKind::Bool,
                   JobCancel),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::IsDone, {TypeKind::I64}, TypeKind::Bool,
                   JobIsDone),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::IsFailed, {TypeKind::I64}, TypeKind::Bool,
                   JobIsFailed),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::IsCancelled, {TypeKind::I64}, TypeKind::Bool,
                   JobIsCancelled),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::Error, {TypeKind::I64}, TypeKind::String,
                   JobError),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      StandardSpec(StandardPromiseMember::Close, {TypeKind::I64}, TypeKind::Unspecified,
                   JobClose),
      NativeResourceAccess::InputOutput, 0));
}

} // namespace

void RegisterSystemJob(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  using Simple::Lang::SystemJobMember;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Job);
  registry.Register(JobResourceSpec(
      WithCapability(MayAllocateHost(MakeSpec(
                         module, Simple::Lang::ToMember(SystemJobMember::Spawn),
                         {TypeKind::I32, TypeKind::I64}, TypeKind::I64, JobSpawn)),
                     "threading"),
      NativeResourceAccess::Output));
  registry.Register(JobResourceSpec(
      WithCapability(MayAllocateHost(MakeSpec(
                         module, Simple::Lang::ToMember(SystemJobMember::SpawnFailed),
                         {TypeKind::I32, TypeKind::String}, TypeKind::I64,
                         JobSpawnFailed)),
                     "threading"),
      NativeResourceAccess::Output));
  registry.Register(JobResourceSpec(
      MakeSpec(module, Simple::Lang::ToMember(SystemJobMember::Cancel), {TypeKind::I64},
               TypeKind::Bool, JobCancel),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      MakeSpec(module, Simple::Lang::ToMember(SystemJobMember::Poll), {TypeKind::I64},
               TypeKind::I32, JobPoll),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      MayBlock(MakeSpec(module, Simple::Lang::ToMember(SystemJobMember::Await),
                        {TypeKind::I64}, TypeKind::I64, JobAwait)),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      MakeSpec(module, Simple::Lang::ToMember(SystemJobMember::Error), {TypeKind::I64},
               TypeKind::String, JobError),
      NativeResourceAccess::Input, 0));
  registry.Register(JobResourceSpec(
      MakeSpec(module, Simple::Lang::ToMember(SystemJobMember::Close), {TypeKind::I64},
               TypeKind::Unspecified, JobClose),
      NativeResourceAccess::InputOutput, 0));
  RegisterPromiseSurface(registry);
}

} // namespace Simple::VM::Native
