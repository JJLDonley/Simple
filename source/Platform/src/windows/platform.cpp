#include "platform/platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <vector>

namespace Simple::Platform {
namespace {
void SetError(std::string* error, const std::string& value) { if (error) *error = value; }
std::string WindowsError(const char* fallback) {
  const DWORD code = GetLastError();
  char* text = nullptr;
  const DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                    nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
  std::string result = size && text ? std::string(text, size) : fallback;
  if (text) LocalFree(text);
  return result;
}
std::string Quote(const std::filesystem::path& path) { return "\"" + path.string() + "\""; }
}

OperatingSystem HostOperatingSystem() { return OperatingSystem::Windows; }
const char* OperatingSystemName() { return "windows"; }
const char* SharedLibraryExtension() { return ".dll"; }
const char* StaticLibraryExtension() { return ".lib"; }

std::string ExecutablePath(const char* argv0) {
  std::vector<wchar_t> buffer(32768);
  const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size) return std::filesystem::path(std::wstring(buffer.data(), size)).string();
  if (!argv0 || !*argv0) return {};
  return std::filesystem::absolute(argv0).string();
}

bool GetEnvironment(const std::string& name, std::string* value) {
  if (!value) return false;
  char* raw = nullptr;
  size_t size = 0;
  if (_dupenv_s(&raw, &size, name.c_str()) != 0 || !raw) return false;
  value->assign(raw, size ? size - 1 : 0);
  std::free(raw);
  return true;
}
bool SetEnvironment(const std::string& name, const std::string& value) { return _putenv_s(name.c_str(), value.c_str()) == 0; }
bool UnsetEnvironment(const std::string& name) { return _putenv_s(name.c_str(), "") == 0; }

int64_t OpenDynamicLibrary(const std::string& path, std::string* error) {
  HMODULE handle = LoadLibraryW(std::filesystem::path(path).wstring().c_str());
  if (!handle) { SetError(error, WindowsError("System.FFI.open failed")); return 0; }
  if (error) error->clear();
  return reinterpret_cast<int64_t>(handle);
}
int64_t FindDynamicSymbol(int64_t handle, const std::string& name, std::string* error) {
  if (!handle) { SetError(error, "System.FFI.sym null handle"); return 0; }
  FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str());
  if (!symbol) { SetError(error, WindowsError("System.FFI.sym failed")); return 0; }
  if (error) error->clear();
  return reinterpret_cast<int64_t>(symbol);
}
bool CloseDynamicLibrary(int64_t handle, std::string* error) {
  if (!handle) { SetError(error, "System.FFI.close null handle"); return false; }
  if (!FreeLibrary(reinterpret_cast<HMODULE>(handle))) { SetError(error, WindowsError("System.FFI.close failed")); return false; }
  if (error) error->clear();
  return true;
}

bool BuildNativeExecutable(const NativeBuildRequest& request, std::string* error) {
  std::string configured;
  GetEnvironment("CXX", &configured);
  std::string command = configured.empty() ? "cl.exe" : configured;
  command += " /nologo /std:c++17 /O2 /EHsc /MT";
  for (const auto& include : request.include_dirs) command += " /I" + Quote(include);
  command += " " + Quote(request.source);
  for (const auto& library : request.libraries) command += " " + Quote(library);
  command += " ffi.lib ";
  command += request.extra_link_flags;
  command += " /Fe:" + Quote(request.output);
  if (std::system(command.c_str()) == 0) return true;
  SetError(error, "failed to compile embedded executable with the MSVC C++ toolchain");
  return false;
}

} // namespace Simple::Platform
