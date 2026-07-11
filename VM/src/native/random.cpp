#include "native/random.h"

#include <mutex>
#include <random>

namespace Simple::VM::Native::Random {
namespace {

std::mutex g_mutex;
std::mt19937_64 g_engine{std::random_device{}()};

} // namespace

void Seed(uint64_t value) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_engine.seed(value);
}

int32_t I32() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<int32_t>(g_engine() & 0x7fffffffu);
}

int64_t I64() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<int64_t>(g_engine() & 0x7fffffffffffffffull);
}

int32_t Range(int32_t lo, int32_t hi) {
  if (hi <= lo) return lo;
  std::uniform_int_distribution<int32_t> dist(lo, hi - 1);
  std::lock_guard<std::mutex> lock(g_mutex);
  return dist(g_engine);
}

double F64() {
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  std::lock_guard<std::mutex> lock(g_mutex);
  return dist(g_engine);
}

} // namespace Simple::VM::Native::Random
