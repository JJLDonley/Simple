#include "RAST/import_index.h"

#include "RAST/import_paths.h"

#include <cctype>
#include <fstream>
#include <system_error>

namespace Simple::Lang::RAST {

namespace {

bool ReadFileText(const std::filesystem::path& path, std::string* out) {
  if (!out) return false;
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  *out = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return true;
}

} // namespace

bool ExtractModuleHeaderName(const std::string& text, std::string* out) {
  if (!out) return false;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    const size_t comment = line.find("//");
    if (comment != std::string::npos) line = line.substr(0, comment);
    const size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
      if (end == text.size()) break;
      start = end + 1;
      continue;
    }
    if (line.compare(first, 6, "module") != 0 ||
        (first + 6 < line.size() && !std::isspace(static_cast<unsigned char>(line[first + 6])))) {
      return false;
    }
    size_t pos = first + 6;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
    size_t name_end = pos;
    while (name_end < line.size()) {
      const char c = line[name_end];
      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) break;
      ++name_end;
    }
    if (name_end == pos) return false;
    *out = line.substr(pos, name_end - pos);
    return true;
  }
  return false;
}

bool BuildSimpleFileIndex(const std::filesystem::path& project_root,
                          ImportPathIndex* out) {
  if (!out) return false;
  out->clear();
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      project_root,
      fs::directory_options::skip_permission_denied,
      ec);
  if (ec) return false;
  for (const auto& entry : it) {
    if (!entry.is_regular_file()) continue;
    const fs::path& path = entry.path();
    if (path.extension() != ".simple") continue;
    (*out)[path.filename().string()].push_back(fs::weakly_canonical(path, ec));
    if (ec) {
      ec.clear();
      (*out)[path.filename().string()].push_back(fs::absolute(path));
    }
  }
  return true;
}

bool BuildModuleIndex(const std::filesystem::path& project_root,
                      const ImportPathIndex& file_index,
                      ImportPathIndex* out) {
  if (!out) return false;
  out->clear();
  namespace fs = std::filesystem;
  for (const auto& [_, paths] : file_index) {
    for (const auto& path : paths) {
      std::string text;
      if (!ReadFileText(path, &text)) continue;
      std::string module_name;
      if (ExtractModuleHeaderName(text, &module_name)) {
        (*out)[module_name].push_back(path);
      } else {
        const std::string stem = path.stem().string();
        if (!stem.empty()) {
          (*out)[stem].push_back(path);
          std::string capitalized = stem;
          capitalized[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(capitalized[0])));
          if (capitalized != stem) (*out)[capitalized].push_back(path);
        }
      }
    }
  }
  const fs::path map_path = project_root / "simple.modules";
  std::ifstream map_in(map_path);
  if (map_in) {
    std::string line;
    while (std::getline(map_in, line)) {
      ModuleMapEntry entry;
      if (!ParseModuleMapLine(line, &entry)) continue;
      fs::path path = fs::path(entry.path).is_absolute() ? fs::path(entry.path) : (project_root / entry.path);
      std::error_code ec;
      if (!path.has_extension()) path += ".simple";
      (*out)[entry.name].push_back(fs::weakly_canonical(path, ec));
      if (ec) {
        ec.clear();
        (*out)[entry.name].push_back(fs::absolute(path));
      }
    }
  }
  return true;
}

} // namespace Simple::Lang::RAST
