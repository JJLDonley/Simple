#ifndef SIMPLE_VM_NATIVE_CHANNEL_H
#define SIMPLE_VM_NATIVE_CHANNEL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace Simple::VM::Native::Channel {

template <typename T>
struct State {
  std::mutex mutex;
  std::condition_variable cv;
  std::queue<T> values;
  bool closed = false;
};

template <typename T>
struct Registry {
  std::mutex mutex;
  std::unordered_map<int64_t, std::shared_ptr<State<T>>> channels;
};

extern std::atomic<int64_t> g_next_handle;
extern Registry<int32_t> g_i32;
extern Registry<int64_t> g_i64;
extern Registry<float> g_f32;
extern Registry<double> g_f64;
extern Registry<bool> g_bool;
extern Registry<std::u16string> g_string;
extern Registry<std::vector<int32_t>> g_bytes;

template <typename T>
std::shared_ptr<State<T>> Get(Registry<T>& registry, int64_t handle) {
  std::lock_guard<std::mutex> lock(registry.mutex);
  auto it = registry.channels.find(handle);
  return it == registry.channels.end() ? nullptr : it->second;
}

template <typename T>
int64_t New(Registry<T>& registry) {
  auto state = std::make_shared<State<T>>();
  std::lock_guard<std::mutex> lock(registry.mutex);
  const int64_t handle = g_next_handle.fetch_add(1);
  registry.channels[handle] = std::move(state);
  return handle;
}

template <typename T>
bool Send(Registry<T>& registry, int64_t handle, T value) {
  auto state = Get(registry, handle);
  if (!state) return false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->closed) return false;
    state->values.push(std::move(value));
  }
  state->cv.notify_one();
  return true;
}

template <typename T>
bool Receive(Registry<T>& registry, int64_t handle, bool wait, T* out) {
  auto state = Get(registry, handle);
  if (!state || !out) return false;
  std::unique_lock<std::mutex> lock(state->mutex);
  if (wait) state->cv.wait(lock, [&] { return state->closed || !state->values.empty(); });
  if (state->values.empty()) return false;
  *out = std::move(state->values.front());
  state->values.pop();
  return true;
}

template <typename T>
int32_t Pending(Registry<T>& registry, int64_t handle) {
  auto state = Get(registry, handle);
  if (!state) return 0;
  std::lock_guard<std::mutex> lock(state->mutex);
  const size_t count = state->values.size();
  return count > static_cast<size_t>((std::numeric_limits<int32_t>::max)())
             ? (std::numeric_limits<int32_t>::max)()
             : static_cast<int32_t>(count);
}

template <typename T>
void Close(Registry<T>& registry, int64_t handle) {
  auto state = Get(registry, handle);
  if (!state) return;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->closed = true;
  }
  state->cv.notify_all();
}

void CloseAll(int64_t handle);

} // namespace Simple::VM::Native::Channel

#endif // SIMPLE_VM_NATIVE_CHANNEL_H
