#ifndef SIMPLE_VM_NATIVE_TIME_H
#define SIMPLE_VM_NATIVE_TIME_H

#include <cstdint>
#include <string>

namespace Simple::VM::Native::Time {

int64_t MonotonicNs();
int64_t WallNs();
std::string FormatWallNsUtc(int64_t ns);

} // namespace Simple::VM::Native::Time

#endif // SIMPLE_VM_NATIVE_TIME_H
