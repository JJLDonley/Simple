#ifndef SIMPLE_VM_NATIVE_JSON_H
#define SIMPLE_VM_NATIVE_JSON_H

#include <cstdint>
#include <string>

namespace Simple::VM::Native::Json {

int64_t Parse(const std::string& text);
bool Stringify(int64_t handle, std::string* out);
bool Free(int64_t handle);
bool IsValidText(const std::string& text);

} // namespace Simple::VM::Native::Json

#endif // SIMPLE_VM_NATIVE_JSON_H
