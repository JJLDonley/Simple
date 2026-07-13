#include "platform/platform.h"
#include "../common/posix_process.h"

#include <cstdlib>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <vector>

namespace Simple::Platform {
namespace {
void SetError(std::string* error, const std::string& value) { if (error) *error = value; }
}

OperatingSystem HostOperatingSystem() { return OperatingSystem::macOS; }
const char* OperatingSystemName() { return "macos"; }
const char* SharedLibraryExtension() { return ".dylib"; }
const char* StaticLibraryExtension() { return ".a"; }
char PathListDelimiter() { return ':'; }
int32_t CurrentProcessId() { return static_cast<int32_t>(getpid()); }
int64_t MemoryPageSize() { return static_cast<int64_t>(sysconf(_SC_PAGESIZE)); }
bool UtcTime(std::time_t value, std::tm* out) {
  return out && gmtime_r(&value, out) != nullptr;
}
std::FILE* OpenFile(const std::string& path, const char* mode) {
  return std::fopen(path.c_str(), mode);
}

std::string ExecutablePath(const char* argv0) {
  std::vector<char> buffer(1024);
  uint32_t size = static_cast<uint32_t>(buffer.size());
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    buffer.resize(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) buffer.clear();
  }
  if (!buffer.empty()) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(buffer.data(), error);
    return error ? std::string(buffer.data()) : canonical.string();
  }
  if (!argv0 || !*argv0) return {};
  return std::filesystem::absolute(argv0).string();
}

bool GetEnvironment(const std::string& name, std::string* value) {
  const char* raw = std::getenv(name.c_str());
  if (!raw || !value) return false;
  *value = raw;
  return true;
}
bool SetEnvironment(const std::string& name, const std::string& value) { return setenv(name.c_str(), value.c_str(), 1) == 0; }
bool UnsetEnvironment(const std::string& name) { return unsetenv(name.c_str()) == 0; }

int64_t OpenDynamicLibrary(const std::string& path, std::string* error) {
  dlerror();
  void* handle = dlopen(path.c_str(), RTLD_LAZY);
  if (!handle) { const char* text = dlerror(); SetError(error, text ? text : "dynamic library open failed"); return 0; }
  if (error) error->clear();
  return reinterpret_cast<int64_t>(handle);
}
int64_t FindDynamicSymbol(int64_t handle, const std::string& name, std::string* error) {
  if (!handle) { SetError(error, "System.FFI.sym null handle"); return 0; }
  dlerror();
  void* symbol = dlsym(reinterpret_cast<void*>(handle), name.c_str());
  if (const char* text = dlerror()) { SetError(error, text); return 0; }
  if (error) error->clear();
  return reinterpret_cast<int64_t>(symbol);
}
bool CloseDynamicLibrary(int64_t handle, std::string* error) {
  if (!handle) { SetError(error, "System.FFI.close null handle"); return false; }
  if (dlclose(reinterpret_cast<void*>(handle)) != 0) { const char* text = dlerror(); SetError(error, text ? text : "dynamic library close failed"); return false; }
  if (error) error->clear();
  return true;
}

bool BuildNativeExecutable(const NativeBuildRequest& request, std::string* error) {
  const char* configured = std::getenv("CXX");
  std::vector<std::string> arguments =
      Posix::SplitArguments(configured && *configured ? configured : "c++");
  arguments.insert(arguments.end(), {"-std=c++17", "-O2", "-Wall", "-Wextra"});
  for (const auto& include : request.include_dirs) arguments.push_back("-I" + include.string());
  arguments.push_back(request.source.string());
  for (const auto& library : request.libraries) arguments.push_back(library.string());
  if (request.dynamic_runtime) arguments.push_back("-Wl,-rpath," + request.runtime_library_dir.string());
  arguments.push_back("-lffi");
  auto extra = Posix::SplitArguments(request.extra_link_flags);
  arguments.insert(arguments.end(), extra.begin(), extra.end());
  arguments.insert(arguments.end(), {"-o", request.output.string()});
  if (Posix::RunProcess(arguments) == 0) return true;
  SetError(error, "failed to compile embedded executable with the macOS C++ toolchain");
  return false;
}

} // namespace Simple::Platform
