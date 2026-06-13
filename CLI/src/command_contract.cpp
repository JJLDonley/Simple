#include "command_contract.h"

#include <filesystem>

namespace Simple::CLI {

bool HasExtension(const std::string& path, const std::string& extension) {
  return std::filesystem::path(path).extension() == extension;
}

bool IsSimpleSourcePath(const std::string& path) {
  return HasExtension(path, ".simple");
}

bool IsSirPath(const std::string& path) {
  return HasExtension(path, ".sir");
}

bool IsSbcPath(const std::string& path) {
  return HasExtension(path, ".sbc");
}

} // namespace Simple::CLI
