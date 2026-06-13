#pragma once

#include <string>

namespace Simple::CLI {

std::string DiagnosticCodeFor(const std::string& message);
std::string DiagnosticHelpFor(const std::string& message);
std::string RenderErrorLine(const std::string& message);

} // namespace Simple::CLI
