#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Simple::CLI {

struct BuildLayoutPaths {
  std::string vm_include;
  std::string byte_include;
  std::string lib_dir;
};

bool ResolveBuildLayoutPaths(const char* argv0, BuildLayoutPaths* out);
bool WriteEmbeddedRunner(const std::string& path,
                         const std::vector<uint8_t>& bytes,
                         std::string* error);
bool BuildEmbeddedExecutable(const BuildLayoutPaths& layout,
                             const std::vector<uint8_t>& bytes,
                             const std::string& out_path,
                             bool is_static,
                             std::string* error);

} // namespace Simple::CLI
