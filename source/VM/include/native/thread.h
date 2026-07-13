#ifndef SIMPLE_VM_NATIVE_THREAD_H
#define SIMPLE_VM_NATIVE_THREAD_H

#include <cstdint>

namespace Simple::VM::Native::Thread {

void SleepMs(int32_t milliseconds);
void Yield();
int32_t HardwareConcurrency();

} // namespace Simple::VM::Native::Thread

#endif // SIMPLE_VM_NATIVE_THREAD_H
