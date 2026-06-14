#include "test_utils.h"

#include <filesystem>
#include <fstream>

#include "RAST/import_index.h"
#include "RAST/import_paths.h"
#include "import_contract.h"

namespace Simple::VM::Tests {
namespace {

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
  {"cli_split_imports_build_shared_simple_file_index", CliSplitImportsBuildSharedSimpleFileIndex},
  {"cli_split_imports_build_shared_module_index", CliSplitImportsBuildSharedModuleIndex},
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
