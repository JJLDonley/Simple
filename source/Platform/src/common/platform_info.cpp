#include "platform/platform.h"

#include <cctype>

namespace Simple::Platform {

Architecture HostArchitecture() {
#if defined(_M_X64) || defined(__x86_64__)
  return Architecture::X86_64;
#elif defined(_M_IX86) || defined(__i386__)
  return Architecture::X86;
#elif defined(_M_ARM64) || defined(__aarch64__)
  return Architecture::Arm64;
#else
  return Architecture::Unknown;
#endif
}

const char* ArchitectureName() {
  switch (HostArchitecture()) {
    case Architecture::X86: return "x86";
    case Architecture::X86_64: return "x86_64";
    case Architecture::Arm64: return "arm64";
    case Architecture::Unknown: return "unknown";
  }
  return "unknown";
}

std::filesystem::path TempDirectory() {
  std::error_code error;
  auto path = std::filesystem::temp_directory_path(error);
  return error ? std::filesystem::path{} : path;
}

std::string NormalizeFileUriPath(std::string path) {
  if (HostOperatingSystem() == OperatingSystem::Windows && path.size() >= 3 &&
      path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
      path[2] == ':') {
    path.erase(path.begin());
  }
  return path;
}

} // namespace Simple::Platform
