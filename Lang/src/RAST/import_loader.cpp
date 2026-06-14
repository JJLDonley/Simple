#include "RAST/import_loader.h"

#include "RAST/import_index.h"
#include "lang_parser.h"
#include "lang_reserved.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace Simple::Lang::RAST {
namespace {

bool ReadFileText(const std::string& path, std::string* out, std::string* error) {
  if (!out) return false;
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "failed to open file: " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  *out = buffer.str();
  return true;
}

} // namespace

bool AppendProgramWithLocalImports(const std::filesystem::path& file_path,
                                   const ImportPathIndex& project_index,
                                   const ImportPathIndex& module_index,
                                   Program* out,
                                   std::unordered_set<std::string>* visiting,
                                   std::unordered_set<std::string>* visited,
                                   std::string* error,
                                   const std::string* override_text) {
  if (!out || !visiting || !visited) return false;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path canon = fs::weakly_canonical(file_path, ec);
  if (ec || canon.empty()) canon = fs::absolute(file_path);
  const std::string key = canon.string();
  if (visited->find(key) != visited->end()) return true;
  if (!visiting->insert(key).second) {
    if (error) *error = "cyclic import detected: " + key;
    return false;
  }

  std::string text;
  if (override_text) {
    text = *override_text;
  } else if (!ReadFileText(key, &text, error)) {
    visiting->erase(key);
    return false;
  }

  Program program;
  std::string parse_error;
  if (!ParseProgramFromString(text, &program, &parse_error)) {
    if (error) *error = key + ": " + parse_error;
    visiting->erase(key);
    return false;
  }

  const fs::path base_dir = canon.parent_path();
  for (const auto& decl : program.decls) {
    if (decl.kind != DeclKind::Import) continue;
    if (decl.import_decl.is_using) continue;
    if (IsReservedImportPath(decl.import_decl.path)) continue;
    fs::path import_file;
    if (!ResolveLocalImportPath(base_dir, project_index, module_index, decl.import_decl.path, &import_file, error)) {
      visiting->erase(key);
      return false;
    }
    if (!AppendProgramWithLocalImports(import_file, project_index, module_index, out, visiting, visited, error)) {
      visiting->erase(key);
      return false;
    }
  }

  for (auto& decl : program.decls) {
    if (decl.kind == DeclKind::ModuleHeader) continue;
    if (decl.kind == DeclKind::Import && !IsReservedImportPath(decl.import_decl.path)) continue;
    out->decls.push_back(std::move(decl));
  }
  for (auto& stmt : program.top_level_stmts) {
    out->top_level_stmts.push_back(std::move(stmt));
  }

  visiting->erase(key);
  visited->insert(key);
  return true;
}

} // namespace Simple::Lang::RAST
