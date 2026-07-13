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

std::string ReplaceExtension(const std::string& path, const std::string& extension) {
  std::filesystem::path out(path);
  out.replace_extension(extension);
  return out.string();
}

std::string DefaultBuildOutputPath(const std::string& input_path, bool build_executable) {
  return ReplaceExtension(input_path, build_executable ? std::string() : std::string(".sbc"));
}

} // namespace Simple::CLI
