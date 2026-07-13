#pragma once

#include <string>

#include "Diagnostics/diagnostic.h"

namespace Simple::LSP {

std::string DiagnosticToLspJson(const Simple::Lang::Diagnostics::Diagnostic& diagnostic);
std::string PublishDiagnosticsMessage(const std::string& uri,
                                      const Simple::Lang::Diagnostics::Diagnostic* diagnostic);

} // namespace Simple::LSP
