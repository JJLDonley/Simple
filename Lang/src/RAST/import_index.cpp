#include "RAST/import_index.h"

#include <system_error>

namespace Simple::Lang::RAST {

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

} // namespace Simple::Lang::RAST
