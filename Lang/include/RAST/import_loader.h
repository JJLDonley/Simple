#pragma once

#include "RAST/import_index.h"
#include "lang_ast.h"

#include <filesystem>
#include <string>
#include <unordered_set>

namespace Simple::Lang::RAST {

bool AppendProgramWithLocalImports(const std::filesystem::path& file_path,
                                   const ImportPathIndex& project_index,
                                   const ImportPathIndex& module_index,
                                   Program* out,
                                   std::unordered_set<std::string>* visiting,
                                   std::unordered_set<std::string>* visited,
                                   std::string* error,
                                   const std::string* override_text = nullptr);

} // namespace Simple::Lang::RAST
