#pragma once

#include <string>

namespace Simple::CLI {

bool HasExtension(const std::string& path, const std::string& extension);
bool IsSimpleSourcePath(const std::string& path);
bool IsSirPath(const std::string& path);
bool IsSbcPath(const std::string& path);

} // namespace Simple::CLI
