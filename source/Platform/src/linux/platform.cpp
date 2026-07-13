#include "platform/platform.h"

#include <cctype>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Simple::Platform {
namespace {
void SetError(std::string* error, const std::string& value) { if (error) *error = value; }
std::vector<std::string> SplitArguments(const std::string& text) {
  std::vector<std::string> arguments;
  std::string current;
  bool quoted = false;
  for (char c : text) {
    if (c == '"' || c == '\'') quoted = !quoted;
    else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
      if (!current.empty()) { arguments.push_back(std::move(current)); current.clear(); }
    } else current += c;
  }
  if (!current.empty()) arguments.push_back(std::move(current));
  return arguments;
}

int RunProcess(const std::vector<std::string>& arguments) {
  if (arguments.empty()) return -1;
  const pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(child, &status, 0) < 0) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
}

OperatingSystem HostOperatingSystem() { return OperatingSystem::Linux; }
const char* OperatingSystemName() { return "linux"; }
const char* SharedLibraryExtension() { return ".so"; }
const char* StaticLibraryExtension() { return ".a"; }

std::string ExecutablePath(const char* argv0) {
  char buffer[4096];
  const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (size > 0) return std::string(buffer, static_cast<size_t>(size));
  if (!argv0 || !*argv0) return {};
  std::error_code error;
  auto path = std::filesystem::weakly_canonical(argv0, error);
  return error ? std::filesystem::absolute(argv0).string() : path.string();
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
  std::vector<std::string> arguments = SplitArguments(configured && *configured ? configured : "c++");
  arguments.insert(arguments.end(), {"-std=c++17", "-O2", "-Wall", "-Wextra"});
  for (const auto& include : request.include_dirs) arguments.push_back("-I" + include.string());
  arguments.push_back(request.source.string());
  for (const auto& library : request.libraries) arguments.push_back(library.string());
  if (request.dynamic_runtime) arguments.push_back("-Wl,-rpath," + request.runtime_library_dir.string());
  arguments.insert(arguments.end(), {"-ldl", "-lffi"});
  auto extra = SplitArguments(request.extra_link_flags);
  arguments.insert(arguments.end(), extra.begin(), extra.end());
  arguments.insert(arguments.end(), {"-o", request.output.string()});
  if (RunProcess(arguments) == 0) return true;
  SetError(error, "failed to compile embedded executable with the Linux C++ toolchain");
  return false;
}

} // namespace Simple::Platform
