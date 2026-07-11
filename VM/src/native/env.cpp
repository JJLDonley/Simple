#include "native/env.h"

#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Simple::VM::Native::Env {

const char* Get(const std::string& name, std::string* owned_value) {
#if defined(_WIN32)
  if (!owned_value) return nullptr;
  char* raw_value = nullptr;
  size_t raw_len = 0;
  if (_dupenv_s(&raw_value, &raw_len, name.c_str()) != 0 || !raw_value) return nullptr;
  owned_value->assign(raw_value, raw_len > 0 ? raw_len - 1 : 0);
  std::free(raw_value);
  return owned_value->c_str();
#else
  (void)owned_value;
  return std::getenv(name.c_str());
#endif
}

bool Set(const std::string& name, const std::string& value) {
#if defined(_WIN32)
  return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
  return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

bool Unset(const std::string& name) {
#if defined(_WIN32)
  return _putenv_s(name.c_str(), "") == 0;
#else
  return unsetenv(name.c_str()) == 0;
#endif
}

std::string PlatformName() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string ArchName() {
#if defined(__x86_64__) || defined(_M_X64)
  return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
  return "x86";
#else
  return "unknown";
#endif
}

std::string ExePath() {
#if defined(_WIN32)
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return n > 0 ? std::string(buf, buf + n) : std::string{};
#elif defined(__linux__)
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    return std::string(buf);
  }
  return {};
#else
  return {};
#endif
}

} // namespace Simple::VM::Native::Env
