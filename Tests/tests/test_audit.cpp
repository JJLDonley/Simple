#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Simple::VM::Tests {
namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string NormalizePath(const std::filesystem::path& path) {
  return path.generic_string();
}

std::vector<std::filesystem::path> CollectFiles(const std::filesystem::path& root,
                                                const std::string& extension) {
  std::vector<std::filesystem::path> files;
  if (!std::filesystem::exists(root)) return files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() == extension) files.push_back(entry.path());
  }
  return files;
}

bool AuditDuplicateTestNames() {
  const std::regex array_begin(R"(static\s+const\s+TestCase\s+([A-Za-z0-9_]+)\s*\[\]\s*=\s*\{)");
  const std::regex test_entry(R"test(\{\s*"([^"]+)"\s*,)test");
  bool ok = true;
  for (const auto& path : CollectFiles("Tests/tests", ".cpp")) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool in_array = false;
    std::string array_name;
    std::unordered_map<std::string, size_t> first_line;
    size_t line_no = 0;
    while (std::getline(in, line)) {
      ++line_no;
      if (!in_array) {
        std::smatch begin_match;
        if (std::regex_search(line, begin_match, array_begin)) {
          in_array = true;
          array_name = begin_match[1].str();
          first_line.clear();
        }
        continue;
      }
      if (line.find("};") != std::string::npos) {
        in_array = false;
        array_name.clear();
        first_line.clear();
        continue;
      }
      std::smatch entry_match;
      if (std::regex_search(line, entry_match, test_entry)) {
        const std::string name = entry_match[1].str();
        auto [it, inserted] = first_line.emplace(name, line_no);
        if (!inserted) {
          std::cerr << "duplicate test name in " << NormalizePath(path) << " array " << array_name
                    << ": " << name << " first line " << it->second << ", duplicate line "
                    << line_no << "\n";
          ok = false;
        }
      }
    }
  }
  return ok;
}

bool AuditSimpleFixturesAreRegistered() {
  std::string test_sources;
  for (const auto& path : CollectFiles("Tests/tests", ".cpp")) {
    test_sources += ReadTextFile(path);
    test_sources.push_back('\n');
  }

  bool ok = true;
  for (const char* root : {"Tests/simple", "Tests/simple_bad"}) {
    for (const auto& path : CollectFiles(root, ".simple")) {
      const std::string fixture = NormalizePath(path);
      if (fixture.find("/simple.modules") != std::string::npos) continue;
      if (test_sources.find(fixture) == std::string::npos) {
        std::cerr << "unregistered Simple fixture: " << fixture << "\n";
        ok = false;
      }
    }
  }
  return ok;
}

bool AuditNoPublicAliasImportsInFixtures() {
  const std::regex legacy_import(R"(^\s*import\s+(IO|FS|DL|Time|Buffer|Channel)\b)");
  bool ok = true;
  for (const auto& path : CollectFiles("Tests", ".simple")) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    size_t line_no = 0;
    while (std::getline(in, line)) {
      ++line_no;
      if (std::regex_search(line, legacy_import)) {
        std::cerr << "legacy public alias import in fixture " << NormalizePath(path) << ":"
                  << line_no << "\n";
        ok = false;
      }
    }
  }
  return ok;
}

bool AuditNoStaleLibraryDiagnosticNames() {
  const std::regex stale_name(
      R"((^|[^.A-Za-z0-9_])(IO\.print|IO\.buffer|DL\.open|File\.(open|close|write))\b)");
  bool ok = true;
  for (const char* root : {"Lang", "VM", "LSP", "CLI", "Tests/tests"}) {
    for (const auto& path : CollectFiles(root, ".cpp")) {
      if (NormalizePath(path).find("Tests/tests/test_audit.cpp") != std::string::npos) continue;
      std::ifstream in(path);
      if (!in) return false;
      std::string line;
      size_t line_no = 0;
      while (std::getline(in, line)) {
        ++line_no;
        if (std::regex_search(line, stale_name)) {
          std::cerr << "stale library diagnostic/member name in " << NormalizePath(path) << ":"
                    << line_no << "\n";
          ok = false;
        }
      }
    }
  }
  return ok;
}

bool AuditNativeRegistryUsesCatalogMemberNames() {
  const std::regex string_member_spec(R"(MakeSpec\(module,\s*\")");
  bool ok = true;
  std::ifstream in("VM/src/native/registry.cpp");
  if (!in) return false;
  std::string line;
  size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (std::regex_search(line, string_member_spec)) {
      std::cerr << "native registry MakeSpec should use catalog ToMember(...) or generated family names at VM/src/native/registry.cpp:"
                << line_no << "\n";
      ok = false;
    }
  }
  return ok;
}

const TestCase kAuditTests[] = {
  {"audit_duplicate_test_names", AuditDuplicateTestNames},
  {"audit_simple_fixtures_registered", AuditSimpleFixturesAreRegistered},
  {"audit_no_public_alias_imports", AuditNoPublicAliasImportsInFixtures},
  {"audit_no_stale_library_diagnostic_names", AuditNoStaleLibraryDiagnosticNames},
  {"audit_native_registry_uses_catalog_member_names", AuditNativeRegistryUsesCatalogMemberNames},
};

const TestSection kAuditSections[] = {
  {"audit", kAuditTests, sizeof(kAuditTests) / sizeof(kAuditTests[0])},
};

} // namespace

const TestSection* GetAuditSections(size_t* count) {
  if (count) *count = sizeof(kAuditSections) / sizeof(kAuditSections[0]);
  return kAuditSections;
}

} // namespace Simple::VM::Tests
