#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>

namespace Simple::VM::Tests {
namespace {

bool RunCommand(const std::string& command) {
  const int result = std::system(command.c_str());
  return result == 0;
}

std::string TempPath(const std::string& name) {
  namespace fs = std::filesystem;
  return (fs::temp_directory_path() / name).string();
}

std::string ReadFileText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::string text;
  in.seekg(0, std::ios::end);
  text.resize(static_cast<size_t>(in.tellg()));
  in.seekg(0, std::ios::beg);
  in.read(text.data(), static_cast<std::streamsize>(text.size()));
  return text;
}

std::string BuildLspFrame(const std::string& payload) {
  return "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
}

bool WriteBinaryFile(const std::string& path, const std::string& data) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return out.good();
}

bool LspInitializeHandshake() {
  const std::string in_path = TempPath("simple_lsp_init_in.txt");
  const std::string out_path = TempPath("simple_lsp_init_out.txt");
  const std::string err_path = TempPath("simple_lsp_init_err.txt");
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) + BuildLspFrame(shutdown_req) + BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("Content-Length:") != std::string::npos &&
         out_contents.find("\"id\":1") != std::string::npos &&
         out_contents.find("\"capabilities\"") != std::string::npos &&
         out_contents.find("\"semanticTokensProvider\"") != std::string::npos;
}

bool LspDidOpenPublishesDiagnostics() {
  const std::string in_path = TempPath("simple_lsp_diag_in.txt");
  const std::string out_path = TempPath("simple_lsp_diag_out.txt");
  const std::string err_path = TempPath("simple_lsp_diag_err.txt");
  const std::string uri = "file:///workspace/bad.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"y = 1;\"}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"method\":\"textDocument/publishDiagnostics\"") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") != std::string::npos &&
         out_contents.find("\"code\":\"E0001\"") != std::string::npos &&
         out_contents.find("undeclared identifier") != std::string::npos;
}

bool LspHoverReturnsIdentifier() {
  const std::string in_path = TempPath("simple_lsp_hover_in.txt");
  const std::string out_path = TempPath("simple_lsp_hover_out.txt");
  const std::string err_path = TempPath("simple_lsp_hover_err.txt");
  const std::string uri = "file:///workspace/hover.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1; foo;\"}}}";
  const std::string hover_req =
      "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/hover\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":0,\"character\":15}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(hover_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":3") != std::string::npos &&
         out_contents.find("\"contents\"") != std::string::npos &&
         out_contents.find("foo") != std::string::npos;
}

bool LspCompletionReturnsItems() {
  const std::string in_path = TempPath("simple_lsp_completion_in.txt");
  const std::string out_path = TempPath("simple_lsp_completion_out.txt");
  const std::string err_path = TempPath("simple_lsp_completion_err.txt");
  const std::string uri = "file:///workspace/complete.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"\"}}}";
  const std::string completion_req =
      "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/completion\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":0,\"character\":0}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(completion_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":4") != std::string::npos &&
         out_contents.find("\"items\"") != std::string::npos &&
         out_contents.find("\"fn\"") != std::string::npos &&
         out_contents.find("IO.println") != std::string::npos;
}

const TestCase kLspTests[] = {
  {"lsp_initialize_handshake", LspInitializeHandshake},
  {"lsp_did_open_publishes_diagnostics", LspDidOpenPublishesDiagnostics},
  {"lsp_hover_returns_identifier", LspHoverReturnsIdentifier},
  {"lsp_completion_returns_items", LspCompletionReturnsItems},
};

const TestSection kLspSections[] = {
  {"lsp", kLspTests, sizeof(kLspTests) / sizeof(kLspTests[0])},
};

} // namespace

const TestSection* GetLspSections(size_t* count) {
  if (count) *count = sizeof(kLspSections) / sizeof(kLspSections[0]);
  return kLspSections;
}

} // namespace Simple::VM::Tests
