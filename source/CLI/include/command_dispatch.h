#pragma once

#include <string>

namespace Simple::CLI {

struct ToolMode {
  bool simple_only = false;
  bool svm_mode = false;
  bool compiler_frontend = false;
};

ToolMode DetectToolMode(const std::string& tool_name);
bool IsBuildCommand(const std::string& command);
bool IsKnownCommand(const std::string& command);

} // namespace Simple::CLI
