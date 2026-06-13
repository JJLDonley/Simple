#include "native/thread.h"

#include <chrono>
#include <thread>

namespace Simple::VM::Native::Thread {

void SleepMs(int32_t milliseconds) {
  if (milliseconds > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  }
}

void Yield() {
  std::this_thread::yield();
}

int32_t HardwareConcurrency() {
  const unsigned int count = std::thread::hardware_concurrency();
  return static_cast<int32_t>(count == 0 ? 1 : count);
}

} // namespace Simple::VM::Native::Thread
