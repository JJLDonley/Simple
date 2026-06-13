#ifndef SIMPLE_VM_NATIVE_PATH_H
#define SIMPLE_VM_NATIVE_PATH_H

#include <string>

namespace Simple::VM::Native::Path {

std::string Join(const std::string& left, const std::string& right);
std::string Dirname(const std::string& value);
std::string Basename(const std::string& value);
std::string Extension(const std::string& value);
std::string Normalize(const std::string& value);
bool Exists(const std::string& value);
bool IsFile(const std::string& value);
bool IsDir(const std::string& value);

} // namespace Simple::VM::Native::Path

#endif // SIMPLE_VM_NATIVE_PATH_H
