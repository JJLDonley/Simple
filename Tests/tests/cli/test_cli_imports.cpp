#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>

#include "RAST/import_index.h"
#include "RAST/import_loader.h"
#include "RAST/import_paths.h"
#include "import_contract.h"

namespace Simple::VM::Tests {
namespace {

bool CliSplitImportsNoCliLspDuplicateImportWrappers() {
  const char* paths[] = {"CLI/src/main.cpp", "LSP/src/lsp_server.cpp"};
  for (const char* path : paths) {
    std::ifstream in(path);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("BuildSimpleFileIndex(") != std::string::npos) return false;
    if (text.find("BuildModuleIndex(") != std::string::npos) return false;
    if (text.find("AppendProgramWithLocalImports(") != std::string::npos) return false;
    if (text.find("ResolveLocalImportPath(") != std::string::npos) return false;
    if (text.find("RAST/import_index.h") != std::string::npos) return false;
    if (text.find("RAST/import_paths.h") != std::string::npos) return false;
  }
  return true;
}

bool CliSplitImportsBuildSharedSimpleFileIndex() {
  const auto dir = std::filesystem::temp_directory_path() / "simple_cli_import_index_test";
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "Thing.simple");
    out << "main : i32 () { return 0 }";
  }
  Simple::Lang::RAST::ImportPathIndex index;
  const bool ok = Simple::Lang::RAST::BuildSimpleFileIndex(dir, &index);
  std::filesystem::remove_all(dir);
  return ok && index.find("Thing.simple") != index.end() && index["Thing.simple"].size() == 1;
}

bool CliSplitImportsBuildSharedModuleIndex() {
  const auto dir = std::filesystem::temp_directory_path() / "simple_cli_module_index_test";
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "widget.simple");
    out << "module Tools.Widget\nmain : i32 () { return 0 }";
  }
  Simple::Lang::RAST::ImportPathIndex files;
  Simple::Lang::RAST::ImportPathIndex modules;
  const bool ok = Simple::Lang::RAST::BuildSimpleFileIndex(dir, &files) &&
                  Simple::Lang::RAST::BuildModuleIndex(dir, files, &modules);
  std::filesystem::remove_all(dir);
  return ok && modules.find("Tools.Widget") != modules.end();
}

bool CliSplitImportsLoadProgramWithSharedEntryPoint() {
  const auto dir = std::filesystem::temp_directory_path() / "simple_cli_load_imports_test";
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "lib.simple");
    out << "module Load.Lib\nloaded : i32 () { return 2 }";
  }
  const auto entry = dir / "main.simple";
  {
    std::ofstream out(entry);
    out << "import Load.Lib\nmain : i32 () { return loaded() }";
  }
  Simple::Lang::Program program;
  std::string error;
  const bool ok = Simple::Lang::RAST::LoadProgramWithImports(entry, &program, &error);
  std::filesystem::remove_all(dir);
  return ok && program.decls.size() == 2;
}

bool CliSplitImportsAppendProgramWithSharedLoader() {
  const auto dir = std::filesystem::temp_directory_path() / "simple_cli_import_loader_test";
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "lib.simple");
    out << "module Lib\nvalue : i32 () { return 1 }";
  }
  const auto entry = dir / "main.simple";
  {
    std::ofstream out(entry);
    out << "import Lib\nmain : i32 () { return value() }";
  }
  Simple::Lang::RAST::ImportPathIndex files;
  Simple::Lang::RAST::ImportPathIndex modules;
  Simple::Lang::Program program;
  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  std::string error;
  const bool ok = Simple::Lang::RAST::BuildSimpleFileIndex(dir, &files) &&
                  Simple::Lang::RAST::BuildModuleIndex(dir, files, &modules) &&
                  Simple::Lang::RAST::AppendProgramWithLocalImports(
                      entry, files, modules, &program, &visiting, &visited, &error);
  std::filesystem::remove_all(dir);
  return ok && program.decls.size() == 2;
}

bool CliSplitImportsResolveSharedModuleImport() {
  const auto dir = std::filesystem::temp_directory_path() / "simple_cli_resolve_import_test";
  std::filesystem::create_directories(dir);
  const auto file = dir / "widget.simple";
  {
    std::ofstream out(file);
    out << "module Resolve.Widget\nmain : i32 () { return 0 }";
  }
  Simple::Lang::RAST::ImportPathIndex files;
  Simple::Lang::RAST::ImportPathIndex modules;
  std::filesystem::path resolved;
  std::string error;
  const bool ok = Simple::Lang::RAST::BuildSimpleFileIndex(dir, &files) &&
                  Simple::Lang::RAST::BuildModuleIndex(dir, files, &modules) &&
                  Simple::Lang::RAST::ResolveLocalImportPath(
                      dir, files, modules, "Resolve.Widget", &resolved, &error);
  std::filesystem::remove_all(dir);
  return ok && resolved.filename() == "widget.simple";
}

bool CliSplitImportsWriteSharedAutoModuleMap() {
  const auto dir = std::filesystem::temp_directory_path() / "simple_cli_auto_module_map_test";
  std::filesystem::create_directories(dir);
  const auto file = dir / "thing.simple";
  {
    std::ofstream out(file);
    out << "module Auto.Thing\nmain : i32 () { return 0 }";
  }
  Simple::Lang::RAST::ImportPathIndex modules;
  modules["Auto.Thing"].push_back(file);
  const bool ok = Simple::Lang::RAST::WriteAutoModuleMapIfMissing(dir, modules);
  std::ifstream in(dir / "simple.modules");
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove_all(dir);
  return ok && text.find("Auto.Thing=\"thing.simple\"") != std::string::npos;
}

bool CliSplitImportsParseSharedModuleMapLines() {
  Simple::Lang::RAST::ModuleMapEntry entry;
  return Simple::Lang::RAST::ParseModuleMapLine("Math = \"lib/math.simple\" // comment", &entry) &&
         entry.name == "Math" && entry.path == "lib/math.simple";
}

bool CliSplitImportsNormalizesSimplePaths() {
  return Simple::CLI::ImportPathWithSimpleExtension("lib/math") ==
             Simple::Lang::RAST::ImportPathWithSimpleExtension("lib/math") &&
         Simple::CLI::ImportPathWithSimpleExtension("lib/math.simple") == "lib/math.simple" &&
         Simple::CLI::IsExplicitRelativeImportPath("./lib/math") &&
         Simple::CLI::IsExplicitRelativeImportPath("lib/math") &&
         !Simple::CLI::IsExplicitRelativeImportPath("Math");
}

const TestCase kCliImportsTests[] = {
  {"cli_split_imports_no_cli_lsp_duplicate_import_wrappers", CliSplitImportsNoCliLspDuplicateImportWrappers},
  {"cli_split_imports_build_shared_simple_file_index", CliSplitImportsBuildSharedSimpleFileIndex},
  {"cli_split_imports_build_shared_module_index", CliSplitImportsBuildSharedModuleIndex},
  {"cli_split_imports_load_program_with_shared_entry_point", CliSplitImportsLoadProgramWithSharedEntryPoint},
  {"cli_split_imports_append_program_with_shared_loader", CliSplitImportsAppendProgramWithSharedLoader},
  {"cli_split_imports_resolve_shared_module_import", CliSplitImportsResolveSharedModuleImport},
  {"cli_split_imports_write_shared_auto_module_map", CliSplitImportsWriteSharedAutoModuleMap},
  {"cli_split_imports_parse_shared_module_map_lines", CliSplitImportsParseSharedModuleMapLines},
  {"cli_split_imports_normalizes_simple_paths", CliSplitImportsNormalizesSimplePaths},
};

const TestSection kCliImportsSections[] = {
  {"cli_imports", kCliImportsTests, sizeof(kCliImportsTests) / sizeof(kCliImportsTests[0])},
};

} // namespace

const TestSection* GetCliImportsSections(size_t* count) {
  if (count) *count = sizeof(kCliImportsSections) / sizeof(kCliImportsSections[0]);
  return kCliImportsSections;
}

} // namespace Simple::VM::Tests
