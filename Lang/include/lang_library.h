#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Simple::Lang {

enum class LibraryRoot {
  System,
  Standard,
};

enum class SystemModule {
  IO,
  FS,
  Path,
  Env,
  OS,
  Time,
  FFI,
  ASM,
  Buffer,
  Bytes,
  Json,
  Log,
  Random,
  Thread,
  Job,
  Channel,
  Process,
  Net,
  HTTP,
  Terminal,
  Capability,
  Runtime,
  Debug,
};

enum class StandardModule {
  IO,
  Console,
  FS,
  Path,
  Buffer,
  Bytes,
  Text,
  Json,
  Math,
  Random,
  Time,
  Log,
  Process,
  Net,
  HTTP,
  HTTPS,
  Terminal,
  Promise,
  Channel,
  Collections,
  Result,
  Option,
};

enum class SystemBufferMember {
  New,
  Len,
  ReadU16LE,
  ReadU32LE,
  WriteU16LE,
  WriteU32LE,
  Slice,
  Copy,
};

struct LibraryImportInfo {
  LibraryRoot root;
  int module_index;
  std::string_view import_path;
  std::string_view canonical_name;
};

inline constexpr std::array<SystemModule, 23> kSystemModules = {{
    SystemModule::IO,
    SystemModule::FS,
    SystemModule::Path,
    SystemModule::Env,
    SystemModule::OS,
    SystemModule::Time,
    SystemModule::FFI,
    SystemModule::ASM,
    SystemModule::Buffer,
    SystemModule::Bytes,
    SystemModule::Json,
    SystemModule::Log,
    SystemModule::Random,
    SystemModule::Thread,
    SystemModule::Job,
    SystemModule::Channel,
    SystemModule::Process,
    SystemModule::Net,
    SystemModule::HTTP,
    SystemModule::Terminal,
    SystemModule::Capability,
    SystemModule::Runtime,
    SystemModule::Debug,
}};

inline constexpr std::array<StandardModule, 22> kStandardModules = {{
    StandardModule::IO,
    StandardModule::Console,
    StandardModule::FS,
    StandardModule::Path,
    StandardModule::Buffer,
    StandardModule::Bytes,
    StandardModule::Text,
    StandardModule::Json,
    StandardModule::Math,
    StandardModule::Random,
    StandardModule::Time,
    StandardModule::Log,
    StandardModule::Process,
    StandardModule::Net,
    StandardModule::HTTP,
    StandardModule::HTTPS,
    StandardModule::Terminal,
    StandardModule::Promise,
    StandardModule::Channel,
    StandardModule::Collections,
    StandardModule::Result,
    StandardModule::Option,
}};

inline constexpr std::array<SystemBufferMember, 8> kSystemBufferMembers = {{
    SystemBufferMember::New,
    SystemBufferMember::Len,
    SystemBufferMember::ReadU16LE,
    SystemBufferMember::ReadU32LE,
    SystemBufferMember::WriteU16LE,
    SystemBufferMember::WriteU32LE,
    SystemBufferMember::Slice,
    SystemBufferMember::Copy,
}};

inline std::string_view ToImportPath(SystemModule module) {
  switch (module) {
    case SystemModule::IO: return "System.IO";
    case SystemModule::FS: return "System.FS";
    case SystemModule::Path: return "System.Path";
    case SystemModule::Env: return "System.Env";
    case SystemModule::OS: return "System.OS";
    case SystemModule::Time: return "System.Time";
    case SystemModule::FFI: return "System.FFI";
    case SystemModule::ASM: return "System.ASM";
    case SystemModule::Buffer: return "System.Buffer";
    case SystemModule::Bytes: return "System.Bytes";
    case SystemModule::Json: return "System.Json";
    case SystemModule::Log: return "System.Log";
    case SystemModule::Random: return "System.Random";
    case SystemModule::Thread: return "System.Thread";
    case SystemModule::Job: return "System.Job";
    case SystemModule::Channel: return "System.Channel";
    case SystemModule::Process: return "System.Process";
    case SystemModule::Net: return "System.Net";
    case SystemModule::HTTP: return "System.HTTP";
    case SystemModule::Terminal: return "System.Terminal";
    case SystemModule::Capability: return "System.Capability";
    case SystemModule::Runtime: return "System.Runtime";
    case SystemModule::Debug: return "System.Debug";
  }
  return {};
}

inline std::string_view ToImportPath(StandardModule module) {
  switch (module) {
    case StandardModule::IO: return "Standard.IO";
    case StandardModule::Console: return "Standard.Console";
    case StandardModule::FS: return "Standard.FS";
    case StandardModule::Path: return "Standard.Path";
    case StandardModule::Buffer: return "Standard.Buffer";
    case StandardModule::Bytes: return "Standard.Bytes";
    case StandardModule::Text: return "Standard.Text";
    case StandardModule::Json: return "Standard.Json";
    case StandardModule::Math: return "Standard.Math";
    case StandardModule::Random: return "Standard.Random";
    case StandardModule::Time: return "Standard.Time";
    case StandardModule::Log: return "Standard.Log";
    case StandardModule::Process: return "Standard.Process";
    case StandardModule::Net: return "Standard.Net";
    case StandardModule::HTTP: return "Standard.HTTP";
    case StandardModule::HTTPS: return "Standard.HTTPS";
    case StandardModule::Terminal: return "Standard.Terminal";
    case StandardModule::Promise: return "Standard.Promise";
    case StandardModule::Channel: return "Standard.Channel";
    case StandardModule::Collections: return "Standard.Collections";
    case StandardModule::Result: return "Standard.Result";
    case StandardModule::Option: return "Standard.Option";
  }
  return {};
}

inline std::string_view ToMember(SystemBufferMember member) {
  switch (member) {
    case SystemBufferMember::New: return "new";
    case SystemBufferMember::Len: return "len";
    case SystemBufferMember::ReadU16LE: return "readU16LE";
    case SystemBufferMember::ReadU32LE: return "readU32LE";
    case SystemBufferMember::WriteU16LE: return "writeU16LE";
    case SystemBufferMember::WriteU32LE: return "writeU32LE";
    case SystemBufferMember::Slice: return "slice";
    case SystemBufferMember::Copy: return "copy";
  }
  return {};
}

inline std::array<std::string_view, 8> SystemBufferMemberNames() {
  std::array<std::string_view, 8> names{};
  for (size_t i = 0; i < kSystemBufferMembers.size(); ++i) {
    names[i] = ToMember(kSystemBufferMembers[i]);
  }
  return names;
}

// Transitional native names. Phase 3 in Docs/Timeline.md removes the lowercase runtime modules.
inline std::string_view ToNativeModule(SystemModule module) {
  switch (module) {
    case SystemModule::IO: return "System.io";
    case SystemModule::FS: return "System.fs";
    case SystemModule::Path: return "System.path";
    case SystemModule::Env: return "System.env";
    case SystemModule::OS: return "System.os";
    case SystemModule::Time: return "System.os";
    case SystemModule::FFI: return "System.dl";
    case SystemModule::Buffer: return "System.buffer";
    case SystemModule::Bytes: return "System.buffer";
    case SystemModule::Json: return "System.json";
    case SystemModule::Log: return "System.log";
    case SystemModule::Random: return "System.random";
    case SystemModule::Thread: return "System.thread";
    case SystemModule::Channel: return "System.channel";
    default: return {};
  }
}

inline std::string_view ToCanonicalName(SystemModule module) {
  switch (module) {
    case SystemModule::IO: return "SystemIO";
    case SystemModule::FS: return "FS";
    case SystemModule::Path: return "Path";
    case SystemModule::Env: return "Env";
    case SystemModule::OS: return "OS";
    case SystemModule::Time: return "Time";
    case SystemModule::FFI: return "DL";
    case SystemModule::ASM: return "SystemASM";
    case SystemModule::Buffer: return "SystemBuffer";
    case SystemModule::Bytes: return "SystemBytes";
    case SystemModule::Json: return "SystemJson";
    case SystemModule::Log: return "SystemLog";
    case SystemModule::Random: return "SystemRandom";
    case SystemModule::Thread: return "Thread";
    case SystemModule::Job: return "SystemJob";
    case SystemModule::Channel: return "Channel";
    case SystemModule::Process: return "SystemProcess";
    case SystemModule::Net: return "SystemNet";
    case SystemModule::HTTP: return "SystemHTTP";
    case SystemModule::Terminal: return "SystemTerminal";
    case SystemModule::Capability: return "SystemCapability";
    case SystemModule::Runtime: return "SystemRuntime";
    case SystemModule::Debug: return "SystemDebug";
  }
  return {};
}

inline std::string_view ToCanonicalName(StandardModule module) {
  switch (module) {
    case StandardModule::IO: return "StandardIO";
    case StandardModule::Console: return "StandardConsole";
    case StandardModule::FS: return "StandardFS";
    case StandardModule::Path: return "StandardPath";
    case StandardModule::Buffer: return "StandardBuffer";
    case StandardModule::Bytes: return "StandardBytes";
    case StandardModule::Text: return "StandardText";
    case StandardModule::Json: return "StandardJson";
    case StandardModule::Math: return "Math";
    case StandardModule::Random: return "StandardRandom";
    case StandardModule::Time: return "StandardTime";
    case StandardModule::Log: return "StandardLog";
    case StandardModule::Process: return "StandardProcess";
    case StandardModule::Net: return "StandardNet";
    case StandardModule::HTTP: return "StandardHTTP";
    case StandardModule::HTTPS: return "StandardHTTPS";
    case StandardModule::Terminal: return "StandardTerminal";
    case StandardModule::Promise: return "StandardPromise";
    case StandardModule::Channel: return "StandardChannel";
    case StandardModule::Collections: return "StandardCollections";
    case StandardModule::Result: return "StandardResult";
    case StandardModule::Option: return "StandardOption";
  }
  return {};
}

inline std::optional<SystemModule> ParseSystemImportPath(std::string_view path) {
  for (SystemModule module : kSystemModules) {
    if (path == ToImportPath(module)) return module;
  }
  return std::nullopt;
}

inline std::optional<StandardModule> ParseStandardImportPath(std::string_view path) {
  for (StandardModule module : kStandardModules) {
    if (path == ToImportPath(module)) return module;
  }
  return std::nullopt;
}

inline std::optional<LibraryImportInfo> ParseLibraryImportPath(std::string_view path) {
  if (auto module = ParseSystemImportPath(path)) {
    return LibraryImportInfo{LibraryRoot::System, static_cast<int>(*module), ToImportPath(*module),
                             ToCanonicalName(*module)};
  }
  if (auto module = ParseStandardImportPath(path)) {
    return LibraryImportInfo{LibraryRoot::Standard, static_cast<int>(*module), ToImportPath(*module),
                             ToCanonicalName(*module)};
  }
  return std::nullopt;
}

inline bool IsSystemBufferLikeCanonical(std::string_view canonical) {
  return canonical == ToCanonicalName(SystemModule::Buffer) ||
         canonical == ToCanonicalName(SystemModule::Bytes);
}

inline bool IsStandardBufferLikeCanonical(std::string_view canonical) {
  return canonical == ToCanonicalName(StandardModule::Buffer) ||
         canonical == ToCanonicalName(StandardModule::Bytes);
}

inline bool IsSystemBufferMember(std::string_view member) {
  for (SystemBufferMember item : kSystemBufferMembers) {
    if (member == ToMember(item)) return true;
  }
  return false;
}

inline std::optional<std::string_view> LegacyReservedImportReplacementView(std::string_view path) {
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

inline std::array<std::string_view, kSystemModules.size() + kStandardModules.size()> AllLibraryImportPaths() {
  std::array<std::string_view, kSystemModules.size() + kStandardModules.size()> out{};
  size_t index = 0;
  for (SystemModule module : kSystemModules) out[index++] = ToImportPath(module);
  for (StandardModule module : kStandardModules) out[index++] = ToImportPath(module);
  return out;
}

} // namespace Simple::Lang
