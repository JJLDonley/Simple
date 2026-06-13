#include "command_dispatch.h"

namespace Simple::CLI {

ToolMode DetectToolMode(const std::string& tool_name) {
  ToolMode mode;
  mode.simple_only = tool_name == "simple";
  mode.svm_mode = tool_name == "svm" || tool_name == "SVM";
  mode.compiler_frontend = mode.simple_only || mode.svm_mode;
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
