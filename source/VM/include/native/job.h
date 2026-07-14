#ifndef SIMPLE_VM_NATIVE_JOB_H
#define SIMPLE_VM_NATIVE_JOB_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "native/registry.h"

namespace Simple::VM::Native {

struct NativeJobResult {
  bool succeeded = true;
  int64_t value = 0;
  std::string error;

  static NativeJobResult Success(int64_t value);
  static NativeJobResult Failure(std::string error);
};

class NativeJobControl {
 public:
  bool CancellationRequested() const;
  bool WaitFor(std::chrono::milliseconds duration);
  void RequestCancellation();

 private:
  std::atomic<bool> cancellation_requested_{false};
  std::mutex wait_mutex_;
  std::condition_variable wake_;
};

using NativeJobTask = std::function<NativeJobResult(NativeJobControl&)>;

NativeCallResult StartNativeJob(NativeCallContext& context, NativeJobTask task);

} // namespace Simple::VM::Native

#endif // SIMPLE_VM_NATIVE_JOB_H
