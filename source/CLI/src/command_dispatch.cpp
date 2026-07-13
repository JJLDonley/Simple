#include "command_dispatch.h"

namespace Simple::CLI {

ToolMode DetectToolMode(const std::string& tool_name) {
  std::string normalized = tool_name;
  if (normalized.size() > 4 && normalized.compare(normalized.size() - 4, 4, ".exe") == 0) {
    normalized.resize(normalized.size() - 4);
  }
  ToolMode mode;
  mode.simple_only = normalized == "simple";
  mode.svm_mode = normalized == "svm" || normalized == "SVM";
  mode.compiler_frontend = mode.svm_mode;
  return mode;
}

bool IsBuildCommand(const std::string& command) {
  return command == "build" || command == "compile";
}

bool IsKnownCommand(const std::string& command) {
  return command == "run" || IsBuildCommand(command) || command == "check" ||
         command == "emit" || command == "lsp";
}

} // namespace Simple::CLI
