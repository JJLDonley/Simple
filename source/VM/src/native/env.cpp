#include "native/env.h"

#include "platform/platform.h"

namespace Simple::VM::Native::Env {

const char* Get(const std::string& name, std::string* owned_value) {
  if (!owned_value || !Simple::Platform::GetEnvironment(name, owned_value)) return nullptr;
  return owned_value->c_str();
}

bool Set(const std::string& name, const std::string& value) {
  return Simple::Platform::SetEnvironment(name, value);
}

bool Unset(const std::string& name) {
  return Simple::Platform::UnsetEnvironment(name);
}

std::string PlatformName() {
  return Simple::Platform::OperatingSystemName();
}

std::string ArchName() {
  return Simple::Platform::ArchitectureName();
}

std::string ExePath() {
  return Simple::Platform::ExecutablePath();
}

} // namespace Simple::VM::Native::Env
