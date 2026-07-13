#pragma once

#include <filesystem>
#include <string>

namespace Simple::VM::Tests {

std::filesystem::path CliTempPath(const std::string& name);
std::string CliToolPath(const std::string& name);
int CliExitCodeFromSystemResult(int result);
bool RunCliCommandQuiet(const std::string& command);
bool RunCliCommandRaw(const std::string& command);
std::string RunCliCaptureStdout(const std::string& command,
                                const std::string& temp_name,
                                int* out_exit_code = nullptr);
std::string RunCliCaptureStderr(const std::string& command,
                                const std::string& temp_name,
                                int* out_exit_code = nullptr);

} // namespace Simple::VM::Tests
