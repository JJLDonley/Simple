#include "platform/platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <process.h>
#include <iostream>
#include <utility>
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
std::vector<std::string> SplitArguments(const std::string& text) {
  std::vector<std::string> arguments;
  std::string current;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"') {
      quoted = !quoted;
    } else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
      if (!current.empty()) {
        arguments.push_back(std::move(current));
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) arguments.push_back(std::move(current));
  return arguments;
}
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
  const std::string compiler = configured.empty() ? "cl.exe" : configured;
  std::vector<std::string> arguments = {
      compiler, "/nologo", "/std:c++17", "/O2", "/EHsc", "/MT"};
  for (const auto& include : request.include_dirs) {
    arguments.push_back("/I" + include.string());
  }
  arguments.push_back(request.source.string());
  for (const auto& library : request.libraries) arguments.push_back(library.string());
  arguments.push_back((request.runtime_library_dir / "ffi.lib").string());
  auto extra = SplitArguments(request.extra_link_flags);
  for (auto& argument : extra) {
    if (argument.rfind("-L", 0) == 0) {
      argument = "/LIBPATH:" + argument.substr(2);
    } else if (argument.rfind("-l", 0) == 0) {
      argument = argument.substr(2) + ".lib";
    }
  }
  arguments.insert(arguments.end(), extra.begin(), extra.end());
  arguments.push_back("/Fe:" + request.output.string());

  if (std::getenv("SIMPLE_TEST_TRACE")) {
    std::cerr << "[ COMPILER ]";
    for (const auto& argument : arguments) std::cerr << " [" << argument << "]";
    std::cerr << "\n";
  }
  std::vector<const char*> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto& argument : arguments) argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  if (_spawnvp(_P_WAIT, compiler.c_str(), argv.data()) == 0) {
    if (request.dynamic_runtime) {
      std::error_code copy_error;
      const auto runtime_dll = request.runtime_library_dir / "simplevm_runtime.dll";
      std::filesystem::copy_file(
          runtime_dll, request.output.parent_path() / runtime_dll.filename(),
          std::filesystem::copy_options::overwrite_existing, copy_error);
      if (copy_error) {
        SetError(error, "failed to copy the Simple runtime DLL beside the executable");
        return false;
      }
    }
    return true;
  }
  SetError(error, "failed to compile embedded executable with the MSVC C++ toolchain");
  return false;
}

} // namespace Simple::Platform
