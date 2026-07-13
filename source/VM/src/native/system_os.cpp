#include "native/registry.h"

#include "native/env.h"
#include "native/os.h"
#include "native/spec_builder.h"
#include "native/thread.h"
#include "native/time.h"
#include "platform/platform.h"

#include <cstdlib>
#include <string>
#include <thread>
#include <utility>


namespace Simple::VM::Native {
namespace {

NativeCallResult EnvArgsCount(NativeCallContext& context) {
  return NativeCallResult::I32(context.argv ? static_cast<int32_t>(context.argv->size()) : 0);
}

NativeCallResult EnvArg(NativeCallContext& context) {
  int32_t index = 0;
  if (!context.ArgI32(0, &index) || !context.argv || index < 0 ||
      static_cast<size_t>(index) >= context.argv->size()) {
    return NativeCallResult::Ref(HeapLayout::kNullRef);
  }
  return NativeCallResult::String((*context.argv)[static_cast<size_t>(index)]);
}

NativeCallResult EnvGet(NativeCallContext& context) {
  std::string name;
  std::string storage;
  const char* value = context.ArgString(0, &name) ? Env::Get(name, &storage) : nullptr;
  if (!value) return NativeCallResult::Ref(HeapLayout::kNullRef);
  return NativeCallResult::String(value);
}

NativeCallResult EnvSet(NativeCallContext& context) {
  std::string name;
  std::string value;
  return NativeCallResult::I32(context.ArgString(0, &name) && context.ArgString(1, &value) && Env::Set(name, value) ? 1 : 0);
}

NativeCallResult EnvUnset(NativeCallContext& context) {
  std::string name;
  return NativeCallResult::Bool(context.ArgString(0, &name) && Env::Unset(name));
}

NativeCallResult EnvExePath(NativeCallContext&) {
  return NativeCallResult::String(Env::ExePath());
}

NativeCallResult OsTimeMonoNs(NativeCallContext&) {
  return NativeCallResult::I64(Time::MonotonicNs());
}

NativeCallResult OsTimeWallNs(NativeCallContext&) {
  return NativeCallResult::I64(Time::WallNs());
}

NativeCallResult OsCwdGet(NativeCallContext&) {
  std::string cwd;
  if (!Os::CurrentWorkingDirectory(&cwd)) return NativeCallResult::Ref(HeapLayout::kNullRef);
  return NativeCallResult::String(std::move(cwd));
}

NativeCallResult OsPlatform(NativeCallContext&) {
  return NativeCallResult::String(Env::PlatformName());
}

NativeCallResult OsArch(NativeCallContext&) {
  return NativeCallResult::String(Env::ArchName());
}

NativeCallResult OsIsLinux(NativeCallContext&) {
  return NativeCallResult::Bool(
      Simple::Platform::HostOperatingSystem() == Simple::Platform::OperatingSystem::Linux);
}

NativeCallResult OsIsMacos(NativeCallContext&) {
  return NativeCallResult::Bool(
      Simple::Platform::HostOperatingSystem() == Simple::Platform::OperatingSystem::macOS);
}

NativeCallResult OsIsWindows(NativeCallContext&) {
  return NativeCallResult::Bool(
      Simple::Platform::HostOperatingSystem() == Simple::Platform::OperatingSystem::Windows);
}

NativeCallResult OsPid(NativeCallContext&) {
  return NativeCallResult::I32(Simple::Platform::CurrentProcessId());
}

NativeCallResult OsCpuCount(NativeCallContext&) {
  return NativeCallResult::I32(static_cast<int32_t>(std::thread::hardware_concurrency()));
}

NativeCallResult OsPageSize(NativeCallContext&) {
  return NativeCallResult::I32(static_cast<int32_t>(Simple::Platform::MemoryPageSize()));
}

NativeCallResult OsExit(NativeCallContext& context) {
  int32_t code = 0;
  if (!context.ArgI32(0, &code)) return NativeCallResult::Error("System.OS.exit missing code");
  std::exit(code);
}

NativeCallResult OsFormatWallNs(NativeCallContext& context) {
  int64_t ns = 0;
  if (!context.ArgI64(0, &ns)) return NativeCallResult::Error("System.OS.formatWallNs missing timestamp");
  return NativeCallResult::String(Time::FormatWallNsUtc(ns));
}

NativeCallResult OsSleepMs(NativeCallContext& context) {
  int32_t ms = 0;
  if (!context.ArgI32(0, &ms)) return NativeCallResult::Error("System.OS.sleepMs missing duration");
  Thread::SleepMs(ms);
  return NativeCallResult::Void();
}

NativeCallResult ThreadSleep(NativeCallContext& context) {
  int32_t ms = 0;
  if (!context.ArgI32(0, &ms)) return NativeCallResult::Error("System.Thread.sleep missing duration");
  Thread::SleepMs(ms);
  return NativeCallResult::Void();
}

NativeCallResult ThreadYield(NativeCallContext&) {
  (Thread::Yield)();
  return NativeCallResult::Void();
}

NativeCallResult ThreadHardwareConcurrency(NativeCallContext&) {
  return NativeCallResult::I32(Thread::HardwareConcurrency());
}

} // namespace

void RegisterSystemOs(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::OS);
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::ArgsCount), {}, TypeKind::I32, EnvArgsCount),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::ArgsGet), {TypeKind::I32}, TypeKind::String, EnvArg),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::EnvGet), {TypeKind::String}, TypeKind::String, EnvGet),
                                   "environment.read"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::CwdGet), {}, TypeKind::String, OsCwdGet),
                                   "filesystem.read"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::TimeMonoNs), {}, TypeKind::I64, OsTimeMonoNs),
                                   "clock.time"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::TimeWallNs), {}, TypeKind::I64, OsTimeWallNs),
                                   "clock.time"));
  registry.Register(WithStability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::Platform), {}, TypeKind::String, OsPlatform),
                                  NativeStability::Stable));
  registry.Register(WithStability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::Arch), {}, TypeKind::String, OsArch),
                                  NativeStability::Stable));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::IsLinux), {}, TypeKind::Bool, OsIsLinux));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::IsMacos), {}, TypeKind::Bool, OsIsMacos));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::IsWindows), {}, TypeKind::Bool, OsIsWindows));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::Pid), {}, TypeKind::I32, OsPid));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::CpuCount), {}, TypeKind::I32, OsCpuCount));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::PageSize), {}, TypeKind::I32, OsPageSize));
  registry.Register(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::Exit), {TypeKind::I32}, TypeKind::Unspecified, OsExit));
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::SleepMs), {TypeKind::I32},
                                                     TypeKind::Unspecified, OsSleepMs)),
                                   "threading"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemOSMember::FormatWallNs), {TypeKind::I64},
                                            TypeKind::String, OsFormatWallNs),
                                   "clock.time"));
}

void RegisterSystemEnv(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Env);
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemEnvMember::ArgsCount), {}, TypeKind::I32,
                                            EnvArgsCount),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemEnvMember::Arg), {TypeKind::I32},
                                            TypeKind::String, EnvArg),
                                   "process.args"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemEnvMember::Get), {TypeKind::String},
                                            TypeKind::String, EnvGet),
                                   "environment.read"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemEnvMember::Set),
                                            {TypeKind::String, TypeKind::String}, TypeKind::I32,
                                            EnvSet),
                                   "environment.write"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemEnvMember::Unset), {TypeKind::String},
                                            TypeKind::Bool, EnvUnset),
                                   "environment.write"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemEnvMember::ExePath), {}, TypeKind::String,
                                            EnvExePath),
                                   "process.args"));
}

void RegisterSystemThread(NativeRegistry& registry) {
  using Simple::Byte::TypeKind;
  const auto module = Simple::Lang::ToLibraryModuleId(Simple::Lang::SystemModule::Thread);
  registry.Register(WithCapability(MayBlock(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemThreadMember::Sleep), {TypeKind::I32},
                                                     TypeKind::Unspecified, ThreadSleep)),
                                   "threading"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemThreadMember::Yield), {}, TypeKind::Unspecified,
                                            ThreadYield),
                                   "threading"));
  registry.Register(WithCapability(MakeSpec(module, Simple::Lang::ToMember(Simple::Lang::SystemThreadMember::HardwareConcurrency), {},
                                            TypeKind::I32, ThreadHardwareConcurrency),
                                   "threading"));
}

} // namespace Simple::VM::Native
