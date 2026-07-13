#ifndef SIMPLE_VM_NATIVE_LOG_H
#define SIMPLE_VM_NATIVE_LOG_H

#include <cstdint>
#include <string>

namespace Simple::VM::Native::Log {

void SetLevel(int32_t level);
bool SetFile(const std::string& path);
bool Flush();
void Emit(const std::string& message, int32_t level);

} // namespace Simple::VM::Native::Log

#endif // SIMPLE_VM_NATIVE_LOG_H
