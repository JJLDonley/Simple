#ifndef SIMPLE_VM_NATIVE_ENV_H
#define SIMPLE_VM_NATIVE_ENV_H

#include <string>

namespace Simple::VM::Native::Env {

const char* Get(const std::string& name, std::string* owned_value);
bool Set(const std::string& name, const std::string& value);
bool Unset(const std::string& name);
std::string PlatformName();
std::string ArchName();
std::string ExePath();

} // namespace Simple::VM::Native::Env

#endif // SIMPLE_VM_NATIVE_ENV_H
