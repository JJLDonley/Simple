#pragma once

#include <string>

namespace Simple::CLI {

bool HasExtension(const std::string& path, const std::string& extension);
bool IsSimpleSourcePath(const std::string& path);
bool IsSirPath(const std::string& path);
bool IsSbcPath(const std::string& path);
std::string ReplaceExtension(const std::string& path, const std::string& extension);
std::string DefaultBuildOutputPath(const std::string& input_path, bool build_executable);

} // namespace Simple::CLI
