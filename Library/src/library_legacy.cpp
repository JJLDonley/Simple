#include "library_catalog.h"

#include <cctype>

namespace Simple::Lang {

std::string NormalizeSystemFFIMemberName(std::string_view member) {
  if (ParseMember(SystemModule::FFI, member)) return std::string(member);
  if (member == "Open") return std::string(ToMember(SystemFFIMember::Open));
  if (member == "Sym") return std::string(ToMember(SystemFFIMember::Sym));
  if (member == "Close") return std::string(ToMember(SystemFFIMember::Close));
  if (member == "LastError") return std::string(ToMember(SystemFFIMember::LastErrorSnake));
  if (member == "CallI32") return std::string(ToMember(SystemFFIMember::CallI32));
  if (member == "CallI64") return std::string(ToMember(SystemFFIMember::CallI64));
  if (member == "CallF32") return std::string(ToMember(SystemFFIMember::CallF32));
  if (member == "CallF64") return std::string(ToMember(SystemFFIMember::CallF64));
  if (member == "CallStr0") return std::string(ToMember(SystemFFIMember::CallStr0));
  return std::string(member);
}

namespace {

bool EqualsStaleLowercaseRuntimeModule(std::string_view stale,
                                       std::string_view canonical) {
  constexpr std::string_view prefix = "System.";
  if (stale.size() != canonical.size()) return false;
  if (stale.substr(0, prefix.size()) != prefix || canonical.substr(0, prefix.size()) != prefix) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (stale[i] != canonical[i]) return false;
  }
  bool differs = false;
  for (size_t i = prefix.size(); i < canonical.size(); ++i) {
    const char expected = static_cast<char>(std::tolower(static_cast<unsigned char>(canonical[i])));
    if (stale[i] != expected) return false;
    if (stale[i] != canonical[i]) differs = true;
  }
  return differs;
}

} // namespace

std::optional<std::string_view> StaleLowercaseRuntimeModuleReplacement(std::string_view module) {
  for (SystemModule system_module : kSystemModules) {
    const std::string_view canonical = ToImportPath(system_module);
    if (EqualsStaleLowercaseRuntimeModule(module, canonical)) return canonical;
  }
  return std::nullopt;
}

std::optional<std::string_view> LegacyReservedImportReplacementView(std::string_view path) {
  if (path == "IO") return "Standard.IO";
  if (path == "Math") return "Standard.Math";
  if (path == "Time") return "System.Time or Standard.Time";
  if (path == "DL") return "System.FFI";
  if (path == "OS") return "System.OS";
  if (path == "File") return "System.FS";
  if (path == "FS") return "Standard.FS or System.FS";
  if (path == "Path") return "Standard.Path or System.Path";
  if (path == "Env") return "System.Env";
  if (path == "Random") return "Standard.Random or System.Random";
  if (path == "Buffer") return "System.Buffer, Standard.Buffer, System.Bytes, or Standard.Bytes";
  if (path == "Json") return "System.Json or Standard.Json";
  if (path == "Log") return "Standard.Log or System.Log";
  if (path == "Thread") return "System.Thread";
  if (path == "Channel") return "System.Channel";
  if (path == "Http") return "Standard.HTTP or System.HTTP";
  if (path == "Socket") return "Standard.Net or System.Net";
  return std::nullopt;
}

} // namespace Simple::Lang
