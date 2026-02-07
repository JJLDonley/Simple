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

bool LspDidChangeRefreshesDiagnostics() {
  const std::string in_path = TempPath("simple_lsp_change_in.txt");
  const std::string out_path = TempPath("simple_lsp_change_out.txt");
  const std::string err_path = TempPath("simple_lsp_change_err.txt");
  const std::string uri = "file:///workspace/change.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"y = 1;\"}}}";
  const std::string change_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":2},"
      "\"contentChanges\":[{\"text\":\"x : i32 = 1;\\nfoo : i32 = x;\"}]}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(change_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  const size_t first_diag = out_contents.find("\"method\":\"textDocument/publishDiagnostics\"");
  if (first_diag == std::string::npos) return false;
  const size_t second_diag = out_contents.find("\"method\":\"textDocument/publishDiagnostics\"", first_diag + 1);
  if (second_diag == std::string::npos) return false;
  const std::string tail = out_contents.substr(second_diag);
  return err_contents.empty() &&
         out_contents.find("\"code\":\"E0001\"") != std::string::npos &&
         tail.find("\"diagnostics\":[]") != std::string::npos;
}

bool LspDidChangeIgnoresStaleVersion() {
  const std::string in_path = TempPath("simple_lsp_stale_change_in.txt");
  const std::string out_path = TempPath("simple_lsp_stale_change_out.txt");
  const std::string err_path = TempPath("simple_lsp_stale_change_err.txt");
  const std::string uri = "file:///workspace/stale.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":2,"
      "\"text\":\"y = 1;\"}}}";
  const std::string stale_change_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":1},"
      "\"contentChanges\":[{\"text\":\"x : i32 = 1;\"}]}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(stale_change_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);

  size_t diag_count = 0;
  size_t search_pos = 0;
  const std::string marker = "\"method\":\"textDocument/publishDiagnostics\"";
  for (;;) {
    const size_t found = out_contents.find(marker, search_pos);
    if (found == std::string::npos) break;
    ++diag_count;
    search_pos = found + marker.size();
  }

  return err_contents.empty() &&
         diag_count == 1 &&
         out_contents.find("\"code\":\"E0001\"") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") != std::string::npos;
}

bool LspDidChangeIgnoresUnknownDocument() {
  const std::string in_path = TempPath("simple_lsp_unknown_change_in.txt");
  const std::string out_path = TempPath("simple_lsp_unknown_change_out.txt");
  const std::string err_path = TempPath("simple_lsp_unknown_change_err.txt");
  const std::string uri = "file:///workspace/unknown.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string change_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":1},"
      "\"contentChanges\":[{\"text\":\"y = 1;\"}]}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(change_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":1") != std::string::npos &&
         out_contents.find("\"id\":2") != std::string::npos &&
         out_contents.find("\"method\":\"textDocument/publishDiagnostics\"") == std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") == std::string::npos;
}

bool LspDidChangeIgnoresDuplicateVersion() {
  const std::string in_path = TempPath("simple_lsp_dup_change_in.txt");
  const std::string out_path = TempPath("simple_lsp_dup_change_out.txt");
  const std::string err_path = TempPath("simple_lsp_dup_change_err.txt");
  const std::string uri = "file:///workspace/dup.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"y = 1;\"}}}";
  const std::string change_good_v2 =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":2},"
      "\"contentChanges\":[{\"text\":\"x : i32 = 1;\"}]}}";
  const std::string change_bad_v2_dup =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":2},"
      "\"contentChanges\":[{\"text\":\"y = 1;\"}]}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(change_good_v2) +
      BuildLspFrame(change_bad_v2_dup) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);

  size_t diag_count = 0;
  size_t search_pos = 0;
  const std::string marker = "\"method\":\"textDocument/publishDiagnostics\"";
  for (;;) {
    const size_t found = out_contents.find(marker, search_pos);
    if (found == std::string::npos) break;
    ++diag_count;
    search_pos = found + marker.size();
  }
  const size_t first_diag = out_contents.find(marker);
  if (first_diag == std::string::npos) return false;
  const size_t second_diag = out_contents.find(marker, first_diag + marker.size());
  if (second_diag == std::string::npos) return false;
  const std::string second_tail = out_contents.substr(second_diag);

  return err_contents.empty() &&
         diag_count == 2 &&
         out_contents.find("\"code\":\"E0001\"") != std::string::npos &&
         second_tail.find("\"diagnostics\":[]") != std::string::npos;
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

bool LspHoverIncludesDeclaredType() {
  const std::string in_path = TempPath("simple_lsp_hover_type_in.txt");
  const std::string out_path = TempPath("simple_lsp_hover_type_out.txt");
  const std::string err_path = TempPath("simple_lsp_hover_type_err.txt");
  const std::string uri = "file:///workspace/hover_type.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nfoo;\"}}}";
  const std::string hover_req =
      "{\"jsonrpc\":\"2.0\",\"id\":19,\"method\":\"textDocument/hover\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":1}}}";
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
         out_contents.find("\"id\":19") != std::string::npos &&
         out_contents.find("foo : i32") != std::string::npos;
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

bool LspCompletionIncludesLocalDeclarations() {
  const std::string in_path = TempPath("simple_lsp_completion_local_in.txt");
  const std::string out_path = TempPath("simple_lsp_completion_local_out.txt");
  const std::string err_path = TempPath("simple_lsp_completion_local_err.txt");
  const std::string uri = "file:///workspace/complete_local.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"alpha : i32 = 1;\\nbeta : i32 = alpha;\"}}}";
  const std::string completion_req =
      "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"textDocument/completion\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":5}}}";
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
         out_contents.find("\"id\":15") != std::string::npos &&
         out_contents.find("\"label\":\"alpha\"") != std::string::npos &&
         out_contents.find("\"label\":\"beta\"") != std::string::npos;
}

bool LspCompletionFiltersByTypedPrefix() {
  const std::string in_path = TempPath("simple_lsp_completion_prefix_in.txt");
  const std::string out_path = TempPath("simple_lsp_completion_prefix_out.txt");
  const std::string err_path = TempPath("simple_lsp_completion_prefix_err.txt");
  const std::string uri = "file:///workspace/complete_prefix.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"alpha : i32 = 1;\\nbeta : i32 = 2;\\nal\"}}}";
  const std::string completion_req =
      "{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"textDocument/completion\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":2,\"character\":2}}}";
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
         out_contents.find("\"id\":16") != std::string::npos &&
         out_contents.find("\"label\":\"alpha\"") != std::string::npos &&
         out_contents.find("\"label\":\"beta\"") == std::string::npos;
}

bool LspCompletionFiltersMemberSuffixByReceiver() {
  const std::string in_path = TempPath("simple_lsp_completion_member_in.txt");
  const std::string out_path = TempPath("simple_lsp_completion_member_out.txt");
  const std::string err_path = TempPath("simple_lsp_completion_member_err.txt");
  const std::string uri = "file:///workspace/complete_member.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"import \\\"IO\\\"\\nIO.pr\"}}}";
  const std::string completion_req =
      "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"textDocument/completion\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":5}}}";
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
         out_contents.find("\"id\":17") != std::string::npos &&
         out_contents.find("\"label\":\"IO.print\"") != std::string::npos &&
         out_contents.find("\"label\":\"IO.println\"") != std::string::npos &&
         out_contents.find("\"label\":\"import\"") == std::string::npos;
}

bool LspSignatureHelpReturnsSignature() {
  const std::string in_path = TempPath("simple_lsp_signature_help_in.txt");
  const std::string out_path = TempPath("simple_lsp_signature_help_out.txt");
  const std::string err_path = TempPath("simple_lsp_signature_help_err.txt");
  const std::string uri = "file:///workspace/signature.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"import \\\"IO\\\"\\nIO.println(1);\"}}}";
  const std::string signature_req =
      "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/signatureHelp\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":11}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(signature_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"signatureHelpProvider\"") != std::string::npos &&
         out_contents.find("\"id\":10") != std::string::npos &&
         out_contents.find("\"signatures\"") != std::string::npos &&
         out_contents.find("IO.println(value)") != std::string::npos &&
         out_contents.find("\"activeSignature\":0") != std::string::npos &&
         out_contents.find("\"activeParameter\":0") != std::string::npos;
}

bool LspSignatureHelpTracksActiveParameter() {
  const std::string in_path = TempPath("simple_lsp_signature_param_in.txt");
  const std::string out_path = TempPath("simple_lsp_signature_param_out.txt");
  const std::string err_path = TempPath("simple_lsp_signature_param_err.txt");
  const std::string uri = "file:///workspace/signature_param.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"import \\\"IO\\\"\\nIO.println(1, 2);\"}}}";
  const std::string signature_req =
      "{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"textDocument/signatureHelp\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":14}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(signature_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":18") != std::string::npos &&
         out_contents.find("\"activeSignature\":0") != std::string::npos &&
         out_contents.find("\"activeParameter\":1") != std::string::npos;
}

bool LspSemanticTokensReturnsData() {
  const std::string in_path = TempPath("simple_lsp_tokens_in.txt");
  const std::string out_path = TempPath("simple_lsp_tokens_out.txt");
  const std::string err_path = TempPath("simple_lsp_tokens_err.txt");
  const std::string uri = "file:///workspace/tokens.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"import \\\"IO\\\"\\nfoo : i32 = 1;\\nIO.println(foo);\"}}}";
  const std::string tokens_req =
      "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/semanticTokens/full\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(tokens_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":5") != std::string::npos &&
         out_contents.find("\"data\"") != std::string::npos &&
         out_contents.find("\"result\":{\"data\":[") != std::string::npos &&
         out_contents.find("\"result\":{\"data\":[]}") == std::string::npos;
}

bool LspDefinitionReturnsLocation() {
  const std::string in_path = TempPath("simple_lsp_definition_in.txt");
  const std::string out_path = TempPath("simple_lsp_definition_out.txt");
  const std::string err_path = TempPath("simple_lsp_definition_err.txt");
  const std::string uri = "file:///workspace/def.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nbar : i32 = foo;\"}}}";
  const std::string def_req =
      "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/definition\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":12}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(def_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":6") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") != std::string::npos &&
         out_contents.find("\"line\":0") != std::string::npos;
}

bool LspReferencesReturnsLocations() {
  const std::string in_path = TempPath("simple_lsp_references_in.txt");
  const std::string out_path = TempPath("simple_lsp_references_out.txt");
  const std::string err_path = TempPath("simple_lsp_references_err.txt");
  const std::string uri = "file:///workspace/refs.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nfoo = foo + 1;\"}}}";
  const std::string refs_req =
      "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/references\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":7}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(refs_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":7") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") != std::string::npos &&
         out_contents.find("\"line\":0") != std::string::npos &&
         out_contents.find("\"character\":0") != std::string::npos &&
         out_contents.find("\"line\":1") != std::string::npos &&
         out_contents.find("\"character\":6") != std::string::npos;
}

bool LspReferencesCanExcludeDeclaration() {
  const std::string in_path = TempPath("simple_lsp_references_nodecl_in.txt");
  const std::string out_path = TempPath("simple_lsp_references_nodecl_out.txt");
  const std::string err_path = TempPath("simple_lsp_references_nodecl_err.txt");
  const std::string uri = "file:///workspace/refs_nodecl.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nfoo = foo + 1;\"}}}";
  const std::string refs_req =
      "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"textDocument/references\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":7},"
      "\"context\":{\"includeDeclaration\":false}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(refs_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":20") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") != std::string::npos &&
         out_contents.find("\"line\":0") == std::string::npos &&
         out_contents.find("\"line\":1") != std::string::npos &&
         out_contents.find("\"character\":0") != std::string::npos &&
         out_contents.find("\"character\":6") != std::string::npos;
}

bool LspDocumentSymbolReturnsTopLevel() {
  const std::string in_path = TempPath("simple_lsp_symbols_in.txt");
  const std::string out_path = TempPath("simple_lsp_symbols_out.txt");
  const std::string err_path = TempPath("simple_lsp_symbols_err.txt");
  const std::string uri = "file:///workspace/symbols.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nmain : i32 () { return foo; }\"}}}";
  const std::string symbols_req =
      "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"textDocument/documentSymbol\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(symbols_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":8") != std::string::npos &&
         out_contents.find("\"name\":\"foo\"") != std::string::npos &&
         out_contents.find("\"name\":\"main\"") != std::string::npos;
}

bool LspDocumentSymbolMarksFunctionKind() {
  const std::string in_path = TempPath("simple_lsp_symbols_kind_in.txt");
  const std::string out_path = TempPath("simple_lsp_symbols_kind_out.txt");
  const std::string err_path = TempPath("simple_lsp_symbols_kind_err.txt");
  const std::string uri = "file:///workspace/symbols_kind.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"main : i32 () { return 0; }\"}}}";
  const std::string symbols_req =
      "{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"textDocument/documentSymbol\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(symbols_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":23") != std::string::npos &&
         out_contents.find("\"name\":\"main\"") != std::string::npos &&
         out_contents.find("\"kind\":12") != std::string::npos;
}

bool LspWorkspaceSymbolReturnsSymbols() {
  const std::string in_path = TempPath("simple_lsp_workspace_symbols_in.txt");
  const std::string out_path = TempPath("simple_lsp_workspace_symbols_out.txt");
  const std::string err_path = TempPath("simple_lsp_workspace_symbols_err.txt");
  const std::string uri_a = "file:///workspace/a.simple";
  const std::string uri_b = "file:///workspace/b.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_a =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri_a + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"helper : i32 = 1;\\nmain : i32 () { return helper; }\"}}}";
  const std::string open_b =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri_b + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"domain : i32 = 2;\\nmain_worker : i32 = domain;\"}}}";
  const std::string ws_symbols_req =
      "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"main\"}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_a) +
      BuildLspFrame(open_b) +
      BuildLspFrame(ws_symbols_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"workspaceSymbolProvider\":true") != std::string::npos &&
         out_contents.find("\"id\":9") != std::string::npos &&
         out_contents.find("\"name\":\"main\"") != std::string::npos &&
         out_contents.find("\"name\":\"main_worker\"") != std::string::npos &&
         out_contents.find("\"name\":\"domain\"") == std::string::npos &&
         out_contents.find("\"uri\":\"" + uri_a + "\"") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri_b + "\"") != std::string::npos;
}

bool LspWorkspaceSymbolMarksFunctionKind() {
  const std::string in_path = TempPath("simple_lsp_workspace_symbols_kind_in.txt");
  const std::string out_path = TempPath("simple_lsp_workspace_symbols_kind_out.txt");
  const std::string err_path = TempPath("simple_lsp_workspace_symbols_kind_err.txt");
  const std::string uri = "file:///workspace/ws_symbol_kind.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"main : i32 () { return 0; }\"}}}";
  const std::string ws_symbols_req =
      "{\"jsonrpc\":\"2.0\",\"id\":24,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"main\"}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(ws_symbols_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":24") != std::string::npos &&
         out_contents.find("\"name\":\"main\"") != std::string::npos &&
         out_contents.find("\"kind\":12") != std::string::npos;
}

bool LspRenameReturnsWorkspaceEdit() {
  const std::string in_path = TempPath("simple_lsp_rename_in.txt");
  const std::string out_path = TempPath("simple_lsp_rename_out.txt");
  const std::string err_path = TempPath("simple_lsp_rename_err.txt");
  const std::string uri = "file:///workspace/rename.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nfoo = foo + 1;\"}}}";
  const std::string rename_req =
      "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/rename\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":7},"
      "\"newName\":\"bar\"}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(rename_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"renameProvider\"") != std::string::npos &&
         out_contents.find("\"id\":11") != std::string::npos &&
         out_contents.find("\"changes\"") != std::string::npos &&
         out_contents.find("\"newText\":\"bar\"") != std::string::npos &&
         out_contents.find("\"line\":0") != std::string::npos &&
         out_contents.find("\"line\":1") != std::string::npos &&
         out_contents.find("\"character\":6") != std::string::npos;
}

bool LspPrepareRenameReturnsRangeAndPlaceholder() {
  const std::string in_path = TempPath("simple_lsp_prepare_rename_in.txt");
  const std::string out_path = TempPath("simple_lsp_prepare_rename_out.txt");
  const std::string err_path = TempPath("simple_lsp_prepare_rename_err.txt");
  const std::string uri = "file:///workspace/prepare_rename.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1;\\nfoo = foo + 1;\"}}}";
  const std::string prepare_req =
      "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"textDocument/prepareRename\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":1,\"character\":7}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(prepare_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"prepareProvider\":true") != std::string::npos &&
         out_contents.find("\"id\":14") != std::string::npos &&
         out_contents.find("\"range\"") != std::string::npos &&
         out_contents.find("\"placeholder\":\"foo\"") != std::string::npos &&
         out_contents.find("\"line\":1") != std::string::npos &&
         out_contents.find("\"character\":6") != std::string::npos;
}

bool LspCodeActionReturnsQuickFix() {
  const std::string in_path = TempPath("simple_lsp_code_action_in.txt");
  const std::string out_path = TempPath("simple_lsp_code_action_out.txt");
  const std::string err_path = TempPath("simple_lsp_code_action_err.txt");
  const std::string uri = "file:///workspace/code_action.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"y = 1;\"}}}";
  const std::string action_req =
      "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/codeAction\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},"
      "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
      "\"context\":{\"diagnostics\":[{\"code\":\"E0001\"}]}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(action_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"codeActionProvider\":true") != std::string::npos &&
         out_contents.find("\"id\":12") != std::string::npos &&
         out_contents.find("\"kind\":\"quickfix\"") != std::string::npos &&
         out_contents.find("Declare 'y' as i32") != std::string::npos &&
         out_contents.find("\"newText\":\"y : i32 = 0;\\n\"") != std::string::npos &&
         out_contents.find("\"uri\":\"" + uri + "\"") != std::string::npos;
}

bool LspCodeActionRespectsOnlyFilter() {
  const std::string in_path = TempPath("simple_lsp_code_action_only_in.txt");
  const std::string out_path = TempPath("simple_lsp_code_action_only_out.txt");
  const std::string err_path = TempPath("simple_lsp_code_action_only_err.txt");
  const std::string uri = "file:///workspace/code_action_only.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"y = 1;\"}}}";
  const std::string action_req =
      "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"textDocument/codeAction\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},"
      "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
      "\"context\":{\"diagnostics\":[{\"code\":\"E0001\"}],\"only\":[\"refactor\"]}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(action_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":21") != std::string::npos &&
         out_contents.find("\"id\":21,\"result\":[]") != std::string::npos;
}

bool LspCodeActionRespectsDiagnosticCodeFilter() {
  const std::string in_path = TempPath("simple_lsp_code_action_code_in.txt");
  const std::string out_path = TempPath("simple_lsp_code_action_code_out.txt");
  const std::string err_path = TempPath("simple_lsp_code_action_code_err.txt");
  const std::string uri = "file:///workspace/code_action_code.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"y = 1;\"}}}";
  const std::string action_req =
      "{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"textDocument/codeAction\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},"
      "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
      "\"context\":{\"diagnostics\":[{\"code\":\"E9999\"}]}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(action_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"id\":22") != std::string::npos &&
         out_contents.find("\"id\":22,\"result\":[]") != std::string::npos &&
         out_contents.find("Declare 'y' as i32") == std::string::npos;
}

bool LspCancelRequestSuppressesResponse() {
  const std::string in_path = TempPath("simple_lsp_cancel_in.txt");
  const std::string out_path = TempPath("simple_lsp_cancel_out.txt");
  const std::string err_path = TempPath("simple_lsp_cancel_err.txt");
  const std::string uri = "file:///workspace/cancel.simple";
  const std::string init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
  const std::string open_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{"
      "\"uri\":\"" + uri + "\",\"languageId\":\"simple\",\"version\":1,"
      "\"text\":\"foo : i32 = 1; foo;\"}}}";
  const std::string cancel_req =
      "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\",\"params\":{\"id\":13}}";
  const std::string hover_req =
      "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/hover\",\"params\":{"
      "\"textDocument\":{\"uri\":\"" + uri + "\"},\"position\":{\"line\":0,\"character\":15}}}";
  const std::string shutdown_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
  const std::string exit_req = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
  const std::string input =
      BuildLspFrame(init_req) +
      BuildLspFrame(open_req) +
      BuildLspFrame(cancel_req) +
      BuildLspFrame(hover_req) +
      BuildLspFrame(shutdown_req) +
      BuildLspFrame(exit_req);
  if (!WriteBinaryFile(in_path, input)) return false;
  const std::string cmd = "cat " + in_path + " | bin/simple lsp 1> " + out_path + " 2> " + err_path;
  if (!RunCommand(cmd)) return false;
  const std::string out_contents = ReadFileText(out_path);
  const std::string err_contents = ReadFileText(err_path);
  return err_contents.empty() &&
         out_contents.find("\"method\":\"textDocument/publishDiagnostics\"") != std::string::npos &&
         out_contents.find("\"id\":13") == std::string::npos &&
         out_contents.find("\"id\":2") != std::string::npos;
}

const TestCase kLspTests[] = {
  {"lsp_initialize_handshake", LspInitializeHandshake},
  {"lsp_did_open_publishes_diagnostics", LspDidOpenPublishesDiagnostics},
  {"lsp_did_change_refreshes_diagnostics", LspDidChangeRefreshesDiagnostics},
  {"lsp_did_change_ignores_stale_version", LspDidChangeIgnoresStaleVersion},
  {"lsp_did_change_ignores_unknown_document", LspDidChangeIgnoresUnknownDocument},
  {"lsp_did_change_ignores_duplicate_version", LspDidChangeIgnoresDuplicateVersion},
  {"lsp_hover_returns_identifier", LspHoverReturnsIdentifier},
  {"lsp_hover_includes_declared_type", LspHoverIncludesDeclaredType},
  {"lsp_completion_returns_items", LspCompletionReturnsItems},
  {"lsp_completion_includes_local_declarations", LspCompletionIncludesLocalDeclarations},
  {"lsp_completion_filters_by_typed_prefix", LspCompletionFiltersByTypedPrefix},
  {"lsp_completion_filters_member_suffix_by_receiver", LspCompletionFiltersMemberSuffixByReceiver},
  {"lsp_signature_help_returns_signature", LspSignatureHelpReturnsSignature},
  {"lsp_signature_help_tracks_active_parameter", LspSignatureHelpTracksActiveParameter},
  {"lsp_semantic_tokens_returns_data", LspSemanticTokensReturnsData},
  {"lsp_definition_returns_location", LspDefinitionReturnsLocation},
  {"lsp_references_returns_locations", LspReferencesReturnsLocations},
  {"lsp_references_can_exclude_declaration", LspReferencesCanExcludeDeclaration},
  {"lsp_document_symbol_returns_top_level", LspDocumentSymbolReturnsTopLevel},
  {"lsp_document_symbol_marks_function_kind", LspDocumentSymbolMarksFunctionKind},
  {"lsp_workspace_symbol_returns_symbols", LspWorkspaceSymbolReturnsSymbols},
  {"lsp_workspace_symbol_marks_function_kind", LspWorkspaceSymbolMarksFunctionKind},
  {"lsp_rename_returns_workspace_edit", LspRenameReturnsWorkspaceEdit},
  {"lsp_prepare_rename_returns_range_and_placeholder", LspPrepareRenameReturnsRangeAndPlaceholder},
  {"lsp_code_action_returns_quick_fix", LspCodeActionReturnsQuickFix},
  {"lsp_code_action_respects_only_filter", LspCodeActionRespectsOnlyFilter},
  {"lsp_code_action_respects_diagnostic_code_filter", LspCodeActionRespectsDiagnosticCodeFilter},
  {"lsp_cancel_request_suppresses_response", LspCancelRequestSuppressesResponse},
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
