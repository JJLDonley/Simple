#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Simple::Lang::RAST {

using ImportPathIndex = std::unordered_map<std::string, std::vector<std::filesystem::path>>;

bool ExtractModuleHeaderName(const std::string& text, std::string* out);
bool BuildSimpleFileIndex(const std::filesystem::path& project_root,
                          ImportPathIndex* out);
bool BuildModuleIndex(const std::filesystem::path& project_root,
                      const ImportPathIndex& file_index,
                      ImportPathIndex* out);

} // namespace Simple::Lang::RAST
