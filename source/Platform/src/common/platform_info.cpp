#include "platform/platform.h"

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

} // namespace Simple::Platform
