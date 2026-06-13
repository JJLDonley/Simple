#include "native/native_time.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Simple::VM::Native::Time {

int64_t MonotonicNs() {
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

int64_t WallNs() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::string FormatWallNsUtc(int64_t ns) {
  const int64_t kNsPerSecond = 1000000000LL;
  int64_t sec = ns / kNsPerSecond;
  int64_t frac = ns % kNsPerSecond;
  if (frac < 0) {
    frac += kNsPerSecond;
    --sec;
  }
  std::time_t tt = static_cast<std::time_t>(sec);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
      << std::setw(9) << std::setfill('0') << frac << 'Z';
  return oss.str();
}

} // namespace Simple::VM::Native::Time
