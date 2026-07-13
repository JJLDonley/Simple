#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Simple::VM::Tests {

std::string CliToolPath(const std::string& name);
int RunProcess(const std::filesystem::path& executable,
               const std::vector<std::string>& arguments,
               const std::filesystem::path& stdin_path = {},
               const std::filesystem::path& stdout_path = {},
               const std::filesystem::path& stderr_path = {});
int RunCliTool(const std::string& tool, const std::vector<std::string>& arguments);
bool RunCliToolQuiet(const std::string& tool, const std::vector<std::string>& arguments);
std::string RunCliToolCaptureStderr(const std::string& tool,
                                    const std::vector<std::string>& arguments,
                                    const std::string& temp_name,
                                    int* out_exit_code = nullptr);
int RunCliSvm(const std::vector<std::string>& arguments);
bool RunCliSvmQuiet(const std::vector<std::string>& arguments);
std::string RunCliSvmCaptureStderr(const std::vector<std::string>& arguments,
                                   const std::string& temp_name,
                                   int* out_exit_code = nullptr);
std::string RunProcessCaptureStdout(const std::filesystem::path& executable,
                                    const std::vector<std::string>& arguments,
                                    const std::string& temp_name,
                                    int* out_exit_code = nullptr);

} // namespace Simple::VM::Tests
