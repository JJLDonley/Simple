#include "native/channel.h"

namespace Simple::VM::Native::Channel {

std::atomic<int64_t> g_next_handle{1};
Registry<int32_t> g_i32;
Registry<int64_t> g_i64;
Registry<float> g_f32;
Registry<double> g_f64;
Registry<bool> g_bool;
Registry<std::u16string> g_string;
Registry<std::vector<int32_t>> g_bytes;

void CloseAll(int64_t handle) {
  Close(g_i32, handle);
  Close(g_i64, handle);
  Close(g_f32, handle);
  Close(g_f64, handle);
  Close(g_bool, handle);
  Close(g_string, handle);
  Close(g_bytes, handle);
}

void DestroyAll(int64_t handle) {
  CloseAll(handle);
  Erase(g_i32, handle);
  Erase(g_i64, handle);
  Erase(g_f32, handle);
  Erase(g_f64, handle);
  Erase(g_bool, handle);
  Erase(g_string, handle);
  Erase(g_bytes, handle);
}

} // namespace Simple::VM::Native::Channel
