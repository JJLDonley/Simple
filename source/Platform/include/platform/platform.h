#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Simple::Platform {

enum class OperatingSystem { Linux, macOS, Windows };
enum class Architecture { X86, X86_64, Arm64, Unknown };

OperatingSystem HostOperatingSystem();
Architecture HostArchitecture();
const char* OperatingSystemName();
const char* ArchitectureName();
const char* SharedLibraryExtension();
const char* StaticLibraryExtension();

std::string ExecutablePath(const char* argv0 = nullptr);
std::filesystem::path TempDirectory();
std::string NormalizeFileUriPath(std::string path);
bool GetEnvironment(const std::string& name, std::string* value);
bool SetEnvironment(const std::string& name, const std::string& value);
bool UnsetEnvironment(const std::string& name);

int64_t OpenDynamicLibrary(const std::string& path, std::string* error);
int64_t FindDynamicSymbol(int64_t handle, const std::string& name, std::string* error);
bool CloseDynamicLibrary(int64_t handle, std::string* error);

struct NativeBuildRequest {
  std::filesystem::path source;
  std::filesystem::path output;
  std::vector<std::filesystem::path> include_dirs;
  std::vector<std::filesystem::path> libraries;
  std::filesystem::path runtime_library_dir;
  std::string extra_link_flags;
  bool dynamic_runtime = false;
};

bool BuildNativeExecutable(const NativeBuildRequest& request, std::string* error);

} // namespace Simple::Platform
