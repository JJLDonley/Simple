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
bool WriteAutoModuleMapIfMissing(const std::filesystem::path& project_root,
                                 const ImportPathIndex& module_index);
bool ResolveProjectRootImportPath(const ImportPathIndex& index,
                                  const std::string& import_path,
                                  std::filesystem::path* out,
                                  std::string* error);
bool ResolveModuleMapImportPath(const std::filesystem::path& base_dir,
                                const std::string& import_path,
                                std::filesystem::path* out);
bool ResolveModuleImportPath(const ImportPathIndex& module_index,
                             const std::string& import_path,
                             std::filesystem::path* out,
                             std::string* error);
bool ResolveLocalImportPath(const std::filesystem::path& base_dir,
                            const ImportPathIndex& project_index,
                            const ImportPathIndex& module_index,
                            const std::string& import_path,
                            std::filesystem::path* out,
                            std::string* error);

} // namespace Simple::Lang::RAST
