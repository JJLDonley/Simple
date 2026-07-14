#include "native/registry.h"

namespace Simple::VM::Native {

NativeRegistry BuildDefaultRegistry() {
  NativeRegistry registry;
  RegisterSystemRandom(registry);
  RegisterSystemOs(registry);
  RegisterSystemThread(registry);
  RegisterSystemJob(registry);
  RegisterSystemProcessFunctions(registry);
  RegisterSystemChannel(registry);
  RegisterSystemJson(registry);
  RegisterSystemLog(registry);
  RegisterSystemBuffer(registry);
  RegisterSystemEnv(registry);
  RegisterSystemPath(registry);
  RegisterSystemFs(registry);
  RegisterSystemIo(registry);
  RegisterSystemDl(registry);
  return registry;
}

} // namespace Simple::VM::Native
