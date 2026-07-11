#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

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

enum class SystemIOMember { Stdin, Stdout, Stderr, Write, WriteText, Flush, BufferNew, BufferLen, BufferFill, BufferCopy };
enum class SystemFSMember { Open, Close, Read, Write, Flush, Seek, Tell, Stat, Exists, IsFile, IsDir, ListDir, NextDirEntry, CloseDir, Mkdir, MkdirAll, Remove, Copy, Rename, Cwd, SetCwd, ReadText, WriteText, ReadBytes, WriteBytes };
enum class SystemPathMember { Separator, Delimiter, IsAbsolute, Normalize, Absolute, Relative, Join, Dirname, Basename, Ext, Stem, Exists, IsFile, IsDir };
enum class SystemEnvMember { ArgsCount, Arg, Get, Set, Unset, ExePath };
enum class SystemOSMember { Platform, Arch, IsLinux, IsMacos, IsWindows, Pid, CpuCount, PageSize, Exit, SleepMs, ArgsCount, ArgsGet, EnvGet, CwdGet, TimeMonoNs, TimeWallNs, FormatWallNs };
enum class SystemTimeMember { MonoNs, WallNs, SleepNs, SleepMs, TimerStart, TimerCancel, MonoSnake, WallSnake };
enum class SystemFFIMember { Supported, Open, Symbol, Sym, Close, LastError, LastErrorSnake, CallI32, CallI64, CallF32, CallF64, CallStr0 };
enum class SystemASMMember { FromC, FromDynASM, Compile, Symbol, LinkStub, LinkAot, CloseUnit, CloseObject };
enum class SystemBytesMember { New, Len, Get, Set, Slice, Copy, ReadU16LE, ReadU32LE, ReadU64LE, WriteU16LE, WriteU32LE, WriteU64LE };
enum class SystemJsonMember { Parse, Free, Stringify, Kind, Get, At, Len, AsString, AsI64, AsF64, AsBool };
enum class SystemLogMember { Log, SetLevel, SetFile, Flush, Info, Warn, Error };
enum class SystemRandomMember { Seed, I32, I64, F64, FillBytes, Range };
enum class SystemThreadMember { Yield, SleepMs, Sleep, HardwareConcurrency, Spawn, Join, Detach };
enum class SystemJobMember { Spawn, Cancel, Poll, Await };
enum class SystemChannelMember { NewI32, SendI32, TrySendI32, RecvI32, TryRecvI32, PendingI32, NewI64, SendI64, TrySendI64, RecvI64, TryRecvI64, PendingI64, NewF32, SendF32, TrySendF32, RecvF32, TryRecvF32, PendingF32, NewF64, SendF64, TrySendF64, RecvF64, TryRecvF64, PendingF64, NewBool, SendBool, TrySendBool, RecvBool, TryRecvBool, PendingBool, NewString, SendString, TrySendString, RecvString, TryRecvString, PendingString, NewBytes, SendBytes, TrySendBytes, RecvBytes, TryRecvBytes, PendingBytes, Close };
enum class SystemProcessMember { Spawn, Wait, Kill, Stdin, Stdout, Stderr };
enum class SystemNetMember { TcpConnect, TcpListen, Accept, Send, Recv, Close, UdpOpen, UdpSendTo, UdpRecvFrom };
enum class SystemHTTPMember { ClientRequest, SetHeader, WriteBody, Send, ResponseStatus, ResponseBody, CloseResponse, ListenHttp, ListenHttps, Accept, WriteResponse, CloseServer };
enum class SystemTerminalMember { Open, Close, EnterRaw, ExitRaw, EnterAltScreen, ExitAltScreen, Size, Clear, ClearLine, MoveCursor, ShowCursor, HideCursor, Write, WriteAt, Flush, PollEvent, ReadEvent };
enum class SystemCapabilityMember { Has, Require, Deny };
enum class SystemRuntimeMember { Version, GcCollect, GcStats, HeapStats, JitEnabled, JitStats };
enum class SystemDebugMember { Trap, Assert, StackTrace, Breakpoint };

enum class StandardIOMember { Print, Println, ReadLine };
enum class StandardConsoleMember { Write, WriteLine, ReadLine, Clear, SetColor, ResetColor };
enum class StandardFSMember { ReadText, WriteText, AppendText, ReadBytes, WriteBytes, Exists, IsFile, IsDir, Copy, Move, Remove, EnsureDir, List, Walk, Mkdir, MkdirAll, ListDir, Cwd, SetCwd };
enum class StandardPathMember { Join, Dirname, Basename, Ext, Stem, Normalize, Absolute, Relative };
enum class StandardBufferMember { New, WithCapacity, Len, Capacity, Clear, WriteBytes, WriteString, WriteU16LE, WriteU32LE, WriteU64LE, ReadU16LE, ReadU32LE, ReadU64LE, ToBytes, FromBytes };
enum class StandardBytesMember { New, FromString, ToString, Concat, Slice, ToHex, FromHex, ToBase64, FromBase64 };
enum class StandardTextMember { Len, IsEmpty, Contains, StartsWith, EndsWith, Trim, Split, Join, Replace };
enum class StandardJsonMember { Parse, Stringify, Get, At, AsString, AsI64, AsF64, AsBool };
enum class StandardMathMember { PI, Abs, Min, Max, Sqrt, Clamp, Lerp };
enum class StandardRandomMember { Seed, I32, I64, Range, F64, Bool, Bytes, FillBytes };
enum class StandardTimeMember { MonoNs, NowNs, SleepMs, FormatWallNs, MonoSnake, WallSnake };
enum class StandardLogMember { Debug, Info, Warn, Error, SetLevel, SetFile };
enum class StandardProcessMember { Run, RunText };
enum class StandardNetMember { Connect, Listen, Read, Write, Close };
enum class StandardHTTPMember { Get, Post, Put, Delete, Serve };
enum class StandardHTTPSMember { Get, Post, Serve };
enum class StandardTerminalMember { Open, Close, WithRaw, WithAltScreen, Clear, Size, MoveCursor, WriteAt, ReadEvent, PollEvent };
enum class StandardPromiseMember { Run, Await, Poll, Cancel, IsDone };
enum class StandardChannelMember { New, Send, TrySend, Recv, TryRecv, Close };
enum class StandardCollectionsMember { List, Map, Set, Queue, Stack };
enum class StandardResultMember { Ok, Err, IsOk, Unwrap };
enum class StandardOptionMember { Some, None, IsSome, Unwrap };

enum class SystemBufferMember { New, Len, Get, Set, Slice, Copy, ReadU16LE, ReadU32LE, ReadU64LE, WriteU16LE, WriteU32LE, WriteU64LE };


using SystemMember = std::variant<SystemIOMember, SystemFSMember, SystemPathMember, SystemEnvMember,
                                  SystemOSMember, SystemTimeMember, SystemFFIMember, SystemASMMember,
                                  SystemBufferMember, SystemBytesMember, SystemJsonMember, SystemLogMember,
                                  SystemRandomMember, SystemThreadMember, SystemJobMember, SystemChannelMember,
                                  SystemProcessMember, SystemNetMember, SystemHTTPMember, SystemTerminalMember,
                                  SystemCapabilityMember, SystemRuntimeMember, SystemDebugMember>;

using StandardMember = std::variant<StandardIOMember, StandardConsoleMember, StandardFSMember,
                                    StandardPathMember, StandardBufferMember, StandardBytesMember,
                                    StandardTextMember, StandardJsonMember, StandardMathMember,
                                    StandardRandomMember, StandardTimeMember, StandardLogMember,
                                    StandardProcessMember, StandardNetMember, StandardHTTPMember,
                                    StandardHTTPSMember, StandardTerminalMember, StandardPromiseMember,
                                    StandardChannelMember, StandardCollectionsMember, StandardResultMember,
                                    StandardOptionMember>;

struct LibraryModuleId {
  LibraryRoot root;
  int module_index;
};

inline bool operator==(LibraryModuleId lhs, LibraryModuleId rhs) {
  return lhs.root == rhs.root && lhs.module_index == rhs.module_index;
}

struct LibraryModuleIdHash {
  size_t operator()(LibraryModuleId id) const {
    return (static_cast<size_t>(id.root) << 16u) ^ static_cast<size_t>(id.module_index);
  }
};

using LibraryModuleSet = std::unordered_set<LibraryModuleId, LibraryModuleIdHash>;
using LibraryModuleAliasMap = std::unordered_map<std::string, LibraryModuleId>;

struct LibrarySymbol {
  LibraryModuleId module;
  uint16_t member_index = 0xffffu;
  std::string_view member_name;
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

inline constexpr std::array<SystemBufferMember, 12> kSystemBufferMembers = {{
    SystemBufferMember::New,
    SystemBufferMember::Len,
    SystemBufferMember::Get,
    SystemBufferMember::Set,
    SystemBufferMember::Slice,
    SystemBufferMember::Copy,
    SystemBufferMember::ReadU16LE,
    SystemBufferMember::ReadU32LE,
    SystemBufferMember::ReadU64LE,
    SystemBufferMember::WriteU16LE,
    SystemBufferMember::WriteU32LE,
    SystemBufferMember::WriteU64LE,
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

inline std::string_view ToMember(SystemIOMember member) {
  switch (member) {
    case SystemIOMember::Stdin: return "stdin";
    case SystemIOMember::Stdout: return "stdout";
    case SystemIOMember::Stderr: return "stderr";
    case SystemIOMember::Write: return "write";
    case SystemIOMember::WriteText: return "writeText";
    case SystemIOMember::Flush: return "flush";
    case SystemIOMember::BufferNew: return "buffer_new";
    case SystemIOMember::BufferLen: return "buffer_len";
    case SystemIOMember::BufferFill: return "buffer_fill";
    case SystemIOMember::BufferCopy: return "buffer_copy";
  }
  return {};
}

inline std::string_view ToMember(SystemFSMember member) {
  switch (member) {
    case SystemFSMember::Open: return "open";
    case SystemFSMember::Close: return "close";
    case SystemFSMember::Read: return "read";
    case SystemFSMember::Write: return "write";
    case SystemFSMember::Flush: return "flush";
    case SystemFSMember::Seek: return "seek";
    case SystemFSMember::Tell: return "tell";
    case SystemFSMember::Stat: return "stat";
    case SystemFSMember::Exists: return "exists";
    case SystemFSMember::IsFile: return "isFile";
    case SystemFSMember::IsDir: return "isDir";
    case SystemFSMember::ListDir: return "listDir";
    case SystemFSMember::NextDirEntry: return "nextDirEntry";
    case SystemFSMember::CloseDir: return "closeDir";
    case SystemFSMember::Mkdir: return "mkdir";
    case SystemFSMember::MkdirAll: return "mkdirAll";
    case SystemFSMember::Remove: return "remove";
    case SystemFSMember::Copy: return "copy";
    case SystemFSMember::Rename: return "rename";
    case SystemFSMember::Cwd: return "cwd";
    case SystemFSMember::SetCwd: return "setCwd";
    case SystemFSMember::ReadText: return "readText";
    case SystemFSMember::WriteText: return "writeText";
    case SystemFSMember::ReadBytes: return "readBytes";
    case SystemFSMember::WriteBytes: return "writeBytes";
  }
  return {};
}

inline std::string_view ToMember(SystemPathMember member) {
  switch (member) {
    case SystemPathMember::Separator: return "separator";
    case SystemPathMember::Delimiter: return "delimiter";
    case SystemPathMember::IsAbsolute: return "isAbsolute";
    case SystemPathMember::Normalize: return "normalize";
    case SystemPathMember::Absolute: return "absolute";
    case SystemPathMember::Relative: return "relative";
    case SystemPathMember::Join: return "join";
    case SystemPathMember::Dirname: return "dirname";
    case SystemPathMember::Basename: return "basename";
    case SystemPathMember::Ext: return "ext";
    case SystemPathMember::Stem: return "stem";
    case SystemPathMember::Exists: return "exists";
    case SystemPathMember::IsFile: return "isFile";
    case SystemPathMember::IsDir: return "isDir";
  }
  return {};
}

inline std::string_view ToMember(SystemEnvMember member) {
  switch (member) {
    case SystemEnvMember::ArgsCount: return "argsCount";
    case SystemEnvMember::Arg: return "arg";
    case SystemEnvMember::Get: return "get";
    case SystemEnvMember::Set: return "set";
    case SystemEnvMember::Unset: return "unset";
    case SystemEnvMember::ExePath: return "exePath";
  }
  return {};
}

inline std::string_view ToMember(SystemOSMember member) {
  switch (member) {
    case SystemOSMember::Platform: return "platform";
    case SystemOSMember::Arch: return "arch";
    case SystemOSMember::IsLinux: return "isLinux";
    case SystemOSMember::IsMacos: return "isMacos";
    case SystemOSMember::IsWindows: return "isWindows";
    case SystemOSMember::Pid: return "pid";
    case SystemOSMember::CpuCount: return "cpuCount";
    case SystemOSMember::PageSize: return "pageSize";
    case SystemOSMember::Exit: return "exit";
    case SystemOSMember::SleepMs: return "sleepMs";
    case SystemOSMember::ArgsCount: return "args_count";
    case SystemOSMember::ArgsGet: return "args_get";
    case SystemOSMember::EnvGet: return "env_get";
    case SystemOSMember::CwdGet: return "cwd_get";
    case SystemOSMember::TimeMonoNs: return "time_mono_ns";
    case SystemOSMember::TimeWallNs: return "time_wall_ns";
    case SystemOSMember::FormatWallNs: return "formatWallNs";
  }
  return {};
}

inline std::string_view ToMember(SystemTimeMember member) {
  switch (member) {
    case SystemTimeMember::MonoNs: return "monoNs";
    case SystemTimeMember::WallNs: return "wallNs";
    case SystemTimeMember::SleepNs: return "sleepNs";
    case SystemTimeMember::SleepMs: return "sleepMs";
    case SystemTimeMember::TimerStart: return "timerStart";
    case SystemTimeMember::TimerCancel: return "timerCancel";
    case SystemTimeMember::MonoSnake: return "mono_ns";
    case SystemTimeMember::WallSnake: return "wall_ns";
  }
  return {};
}

inline std::string_view ToMember(SystemFFIMember member) {
  switch (member) {
    case SystemFFIMember::Supported: return "supported";
    case SystemFFIMember::Open: return "open";
    case SystemFFIMember::Symbol: return "symbol";
    case SystemFFIMember::Sym: return "sym";
    case SystemFFIMember::Close: return "close";
    case SystemFFIMember::LastError: return "lastError";
    case SystemFFIMember::LastErrorSnake: return "last_error";
    case SystemFFIMember::CallI32: return "call_i32";
    case SystemFFIMember::CallI64: return "call_i64";
    case SystemFFIMember::CallF32: return "call_f32";
    case SystemFFIMember::CallF64: return "call_f64";
    case SystemFFIMember::CallStr0: return "call_str0";
  }
  return {};
}

inline std::string_view ToMember(SystemASMMember member) {
  switch (member) {
    case SystemASMMember::FromC: return "fromC";
    case SystemASMMember::FromDynASM: return "fromDynASM";
    case SystemASMMember::Compile: return "compile";
    case SystemASMMember::Symbol: return "symbol";
    case SystemASMMember::LinkStub: return "linkStub";
    case SystemASMMember::LinkAot: return "linkAot";
    case SystemASMMember::CloseUnit: return "closeUnit";
    case SystemASMMember::CloseObject: return "closeObject";
  }
  return {};
}

inline std::string_view ToMember(SystemBytesMember member) {
  switch (member) {
    case SystemBytesMember::New: return "new";
    case SystemBytesMember::Len: return "len";
    case SystemBytesMember::Get: return "get";
    case SystemBytesMember::Set: return "set";
    case SystemBytesMember::Slice: return "slice";
    case SystemBytesMember::Copy: return "copy";
    case SystemBytesMember::ReadU16LE: return "readU16LE";
    case SystemBytesMember::ReadU32LE: return "readU32LE";
    case SystemBytesMember::ReadU64LE: return "readU64LE";
    case SystemBytesMember::WriteU16LE: return "writeU16LE";
    case SystemBytesMember::WriteU32LE: return "writeU32LE";
    case SystemBytesMember::WriteU64LE: return "writeU64LE";
  }
  return {};
}

inline std::string_view ToMember(SystemJsonMember member) {
  switch (member) {
    case SystemJsonMember::Parse: return "parse";
    case SystemJsonMember::Free: return "free";
    case SystemJsonMember::Stringify: return "stringify";
    case SystemJsonMember::Kind: return "kind";
    case SystemJsonMember::Get: return "get";
    case SystemJsonMember::At: return "at";
    case SystemJsonMember::Len: return "len";
    case SystemJsonMember::AsString: return "asString";
    case SystemJsonMember::AsI64: return "asI64";
    case SystemJsonMember::AsF64: return "asF64";
    case SystemJsonMember::AsBool: return "asBool";
  }
  return {};
}

inline std::string_view ToMember(SystemLogMember member) {
  switch (member) {
    case SystemLogMember::Log: return "log";
    case SystemLogMember::SetLevel: return "setLevel";
    case SystemLogMember::SetFile: return "setFile";
    case SystemLogMember::Flush: return "flush";
    case SystemLogMember::Info: return "info";
    case SystemLogMember::Warn: return "warn";
    case SystemLogMember::Error: return "error";
  }
  return {};
}

inline std::string_view ToMember(SystemRandomMember member) {
  switch (member) {
    case SystemRandomMember::Seed: return "seed";
    case SystemRandomMember::I32: return "i32";
    case SystemRandomMember::I64: return "i64";
    case SystemRandomMember::F64: return "f64";
    case SystemRandomMember::FillBytes: return "fillBytes";
    case SystemRandomMember::Range: return "range";
  }
  return {};
}

inline std::string_view ToMember(SystemThreadMember member) {
  switch (member) {
    case SystemThreadMember::Yield: return "yield";
    case SystemThreadMember::SleepMs: return "sleepMs";
    case SystemThreadMember::Sleep: return "sleep";
    case SystemThreadMember::HardwareConcurrency: return "hardwareConcurrency";
    case SystemThreadMember::Spawn: return "spawn";
    case SystemThreadMember::Join: return "join";
    case SystemThreadMember::Detach: return "detach";
  }
  return {};
}

inline std::string_view ToMember(SystemJobMember member) {
  switch (member) {
    case SystemJobMember::Spawn: return "spawn";
    case SystemJobMember::Cancel: return "cancel";
    case SystemJobMember::Poll: return "poll";
    case SystemJobMember::Await: return "await";
  }
  return {};
}

inline std::string_view ToMember(SystemChannelMember member) {
  switch (member) {
    case SystemChannelMember::NewI32: return "newI32";
    case SystemChannelMember::SendI32: return "sendI32";
    case SystemChannelMember::TrySendI32: return "trySendI32";
    case SystemChannelMember::RecvI32: return "recvI32";
    case SystemChannelMember::TryRecvI32: return "tryRecvI32";
    case SystemChannelMember::PendingI32: return "pendingI32";
    case SystemChannelMember::NewI64: return "newI64";
    case SystemChannelMember::SendI64: return "sendI64";
    case SystemChannelMember::TrySendI64: return "trySendI64";
    case SystemChannelMember::RecvI64: return "recvI64";
    case SystemChannelMember::TryRecvI64: return "tryRecvI64";
    case SystemChannelMember::PendingI64: return "pendingI64";
    case SystemChannelMember::NewF32: return "newF32";
    case SystemChannelMember::SendF32: return "sendF32";
    case SystemChannelMember::TrySendF32: return "trySendF32";
    case SystemChannelMember::RecvF32: return "recvF32";
    case SystemChannelMember::TryRecvF32: return "tryRecvF32";
    case SystemChannelMember::PendingF32: return "pendingF32";
    case SystemChannelMember::NewF64: return "newF64";
    case SystemChannelMember::SendF64: return "sendF64";
    case SystemChannelMember::TrySendF64: return "trySendF64";
    case SystemChannelMember::RecvF64: return "recvF64";
    case SystemChannelMember::TryRecvF64: return "tryRecvF64";
    case SystemChannelMember::PendingF64: return "pendingF64";
    case SystemChannelMember::NewBool: return "newBool";
    case SystemChannelMember::SendBool: return "sendBool";
    case SystemChannelMember::TrySendBool: return "trySendBool";
    case SystemChannelMember::RecvBool: return "recvBool";
    case SystemChannelMember::TryRecvBool: return "tryRecvBool";
    case SystemChannelMember::PendingBool: return "pendingBool";
    case SystemChannelMember::NewString: return "newString";
    case SystemChannelMember::SendString: return "sendString";
    case SystemChannelMember::TrySendString: return "trySendString";
    case SystemChannelMember::RecvString: return "recvString";
    case SystemChannelMember::TryRecvString: return "tryRecvString";
    case SystemChannelMember::PendingString: return "pendingString";
    case SystemChannelMember::NewBytes: return "newBytes";
    case SystemChannelMember::SendBytes: return "sendBytes";
    case SystemChannelMember::TrySendBytes: return "trySendBytes";
    case SystemChannelMember::RecvBytes: return "recvBytes";
    case SystemChannelMember::TryRecvBytes: return "tryRecvBytes";
    case SystemChannelMember::PendingBytes: return "pendingBytes";
    case SystemChannelMember::Close: return "close";
  }
  return {};
}

inline std::string_view ToMember(SystemProcessMember member) {
  switch (member) {
    case SystemProcessMember::Spawn: return "spawn";
    case SystemProcessMember::Wait: return "wait";
    case SystemProcessMember::Kill: return "kill";
    case SystemProcessMember::Stdin: return "stdin";
    case SystemProcessMember::Stdout: return "stdout";
    case SystemProcessMember::Stderr: return "stderr";
  }
  return {};
}

inline std::string_view ToMember(SystemNetMember member) {
  switch (member) {
    case SystemNetMember::TcpConnect: return "tcpConnect";
    case SystemNetMember::TcpListen: return "tcpListen";
    case SystemNetMember::Accept: return "accept";
    case SystemNetMember::Send: return "send";
    case SystemNetMember::Recv: return "recv";
    case SystemNetMember::Close: return "close";
    case SystemNetMember::UdpOpen: return "udpOpen";
    case SystemNetMember::UdpSendTo: return "udpSendTo";
    case SystemNetMember::UdpRecvFrom: return "udpRecvFrom";
  }
  return {};
}

inline std::string_view ToMember(SystemHTTPMember member) {
  switch (member) {
    case SystemHTTPMember::ClientRequest: return "clientRequest";
    case SystemHTTPMember::SetHeader: return "setHeader";
    case SystemHTTPMember::WriteBody: return "writeBody";
    case SystemHTTPMember::Send: return "send";
    case SystemHTTPMember::ResponseStatus: return "responseStatus";
    case SystemHTTPMember::ResponseBody: return "responseBody";
    case SystemHTTPMember::CloseResponse: return "closeResponse";
    case SystemHTTPMember::ListenHttp: return "listenHttp";
    case SystemHTTPMember::ListenHttps: return "listenHttps";
    case SystemHTTPMember::Accept: return "accept";
    case SystemHTTPMember::WriteResponse: return "writeResponse";
    case SystemHTTPMember::CloseServer: return "closeServer";
  }
  return {};
}

inline std::string_view ToMember(SystemTerminalMember member) {
  switch (member) {
    case SystemTerminalMember::Open: return "open";
    case SystemTerminalMember::Close: return "close";
    case SystemTerminalMember::EnterRaw: return "enterRaw";
    case SystemTerminalMember::ExitRaw: return "exitRaw";
    case SystemTerminalMember::EnterAltScreen: return "enterAltScreen";
    case SystemTerminalMember::ExitAltScreen: return "exitAltScreen";
    case SystemTerminalMember::Size: return "size";
    case SystemTerminalMember::Clear: return "clear";
    case SystemTerminalMember::ClearLine: return "clearLine";
    case SystemTerminalMember::MoveCursor: return "moveCursor";
    case SystemTerminalMember::ShowCursor: return "showCursor";
    case SystemTerminalMember::HideCursor: return "hideCursor";
    case SystemTerminalMember::Write: return "write";
    case SystemTerminalMember::WriteAt: return "writeAt";
    case SystemTerminalMember::Flush: return "flush";
    case SystemTerminalMember::PollEvent: return "pollEvent";
    case SystemTerminalMember::ReadEvent: return "readEvent";
  }
  return {};
}

inline std::string_view ToMember(SystemCapabilityMember member) {
  switch (member) {
    case SystemCapabilityMember::Has: return "has";
    case SystemCapabilityMember::Require: return "require";
    case SystemCapabilityMember::Deny: return "deny";
  }
  return {};
}

inline std::string_view ToMember(SystemRuntimeMember member) {
  switch (member) {
    case SystemRuntimeMember::Version: return "version";
    case SystemRuntimeMember::GcCollect: return "gcCollect";
    case SystemRuntimeMember::GcStats: return "gcStats";
    case SystemRuntimeMember::HeapStats: return "heapStats";
    case SystemRuntimeMember::JitEnabled: return "jitEnabled";
    case SystemRuntimeMember::JitStats: return "jitStats";
  }
  return {};
}

inline std::string_view ToMember(SystemDebugMember member) {
  switch (member) {
    case SystemDebugMember::Trap: return "trap";
    case SystemDebugMember::Assert: return "assert";
    case SystemDebugMember::StackTrace: return "stackTrace";
    case SystemDebugMember::Breakpoint: return "breakpoint";
  }
  return {};
}

inline std::string_view ToMember(StandardIOMember member) {
  switch (member) {
    case StandardIOMember::Print: return "print";
    case StandardIOMember::Println: return "println";
    case StandardIOMember::ReadLine: return "readLine";
  }
  return {};
}

inline std::string_view ToMember(StandardConsoleMember member) {
  switch (member) {
    case StandardConsoleMember::Write: return "write";
    case StandardConsoleMember::WriteLine: return "writeLine";
    case StandardConsoleMember::ReadLine: return "readLine";
    case StandardConsoleMember::Clear: return "clear";
    case StandardConsoleMember::SetColor: return "setColor";
    case StandardConsoleMember::ResetColor: return "resetColor";
  }
  return {};
}

inline std::string_view ToMember(StandardFSMember member) {
  switch (member) {
    case StandardFSMember::ReadText: return "readText";
    case StandardFSMember::WriteText: return "writeText";
    case StandardFSMember::AppendText: return "appendText";
    case StandardFSMember::ReadBytes: return "readBytes";
    case StandardFSMember::WriteBytes: return "writeBytes";
    case StandardFSMember::Exists: return "exists";
    case StandardFSMember::IsFile: return "isFile";
    case StandardFSMember::IsDir: return "isDir";
    case StandardFSMember::Copy: return "copy";
    case StandardFSMember::Move: return "move";
    case StandardFSMember::Remove: return "remove";
    case StandardFSMember::EnsureDir: return "ensureDir";
    case StandardFSMember::List: return "list";
    case StandardFSMember::Walk: return "walk";
    case StandardFSMember::Mkdir: return "mkdir";
    case StandardFSMember::MkdirAll: return "mkdirAll";
    case StandardFSMember::ListDir: return "listDir";
    case StandardFSMember::Cwd: return "cwd";
    case StandardFSMember::SetCwd: return "setCwd";
  }
  return {};
}

inline std::string_view ToMember(StandardPathMember member) {
  switch (member) {
    case StandardPathMember::Join: return "join";
    case StandardPathMember::Dirname: return "dirname";
    case StandardPathMember::Basename: return "basename";
    case StandardPathMember::Ext: return "ext";
    case StandardPathMember::Stem: return "stem";
    case StandardPathMember::Normalize: return "normalize";
    case StandardPathMember::Absolute: return "absolute";
    case StandardPathMember::Relative: return "relative";
  }
  return {};
}

inline std::string_view ToMember(StandardBufferMember member) {
  switch (member) {
    case StandardBufferMember::New: return "new";
    case StandardBufferMember::WithCapacity: return "withCapacity";
    case StandardBufferMember::Len: return "len";
    case StandardBufferMember::Capacity: return "capacity";
    case StandardBufferMember::Clear: return "clear";
    case StandardBufferMember::WriteBytes: return "writeBytes";
    case StandardBufferMember::WriteString: return "writeString";
    case StandardBufferMember::WriteU16LE: return "writeU16LE";
    case StandardBufferMember::WriteU32LE: return "writeU32LE";
    case StandardBufferMember::WriteU64LE: return "writeU64LE";
    case StandardBufferMember::ReadU16LE: return "readU16LE";
    case StandardBufferMember::ReadU32LE: return "readU32LE";
    case StandardBufferMember::ReadU64LE: return "readU64LE";
    case StandardBufferMember::ToBytes: return "toBytes";
    case StandardBufferMember::FromBytes: return "fromBytes";
  }
  return {};
}

inline std::string_view ToMember(StandardBytesMember member) {
  switch (member) {
    case StandardBytesMember::New: return "new";
    case StandardBytesMember::FromString: return "fromString";
    case StandardBytesMember::ToString: return "toString";
    case StandardBytesMember::Concat: return "concat";
    case StandardBytesMember::Slice: return "slice";
    case StandardBytesMember::ToHex: return "toHex";
    case StandardBytesMember::FromHex: return "fromHex";
    case StandardBytesMember::ToBase64: return "toBase64";
    case StandardBytesMember::FromBase64: return "fromBase64";
  }
  return {};
}

inline std::string_view ToMember(StandardTextMember member) {
  switch (member) {
    case StandardTextMember::Len: return "len";
    case StandardTextMember::IsEmpty: return "isEmpty";
    case StandardTextMember::Contains: return "contains";
    case StandardTextMember::StartsWith: return "startsWith";
    case StandardTextMember::EndsWith: return "endsWith";
    case StandardTextMember::Trim: return "trim";
    case StandardTextMember::Split: return "split";
    case StandardTextMember::Join: return "join";
    case StandardTextMember::Replace: return "replace";
  }
  return {};
}

inline std::string_view ToMember(StandardJsonMember member) {
  switch (member) {
    case StandardJsonMember::Parse: return "parse";
    case StandardJsonMember::Stringify: return "stringify";
    case StandardJsonMember::Get: return "get";
    case StandardJsonMember::At: return "at";
    case StandardJsonMember::AsString: return "asString";
    case StandardJsonMember::AsI64: return "asI64";
    case StandardJsonMember::AsF64: return "asF64";
    case StandardJsonMember::AsBool: return "asBool";
  }
  return {};
}

inline std::string_view ToMember(StandardMathMember member) {
  switch (member) {
    case StandardMathMember::PI: return "PI";
    case StandardMathMember::Abs: return "abs";
    case StandardMathMember::Min: return "min";
    case StandardMathMember::Max: return "max";
    case StandardMathMember::Sqrt: return "sqrt";
    case StandardMathMember::Clamp: return "clamp";
    case StandardMathMember::Lerp: return "lerp";
  }
  return {};
}

inline std::string_view ToMember(StandardRandomMember member) {
  switch (member) {
    case StandardRandomMember::Seed: return "seed";
    case StandardRandomMember::I32: return "i32";
    case StandardRandomMember::I64: return "i64";
    case StandardRandomMember::Range: return "range";
    case StandardRandomMember::F64: return "f64";
    case StandardRandomMember::Bool: return "bool";
    case StandardRandomMember::Bytes: return "bytes";
    case StandardRandomMember::FillBytes: return "fillBytes";
  }
  return {};
}

inline std::string_view ToMember(StandardTimeMember member) {
  switch (member) {
    case StandardTimeMember::MonoNs: return "monoNs";
    case StandardTimeMember::NowNs: return "nowNs";
    case StandardTimeMember::SleepMs: return "sleepMs";
    case StandardTimeMember::FormatWallNs: return "formatWallNs";
    case StandardTimeMember::MonoSnake: return "mono_ns";
    case StandardTimeMember::WallSnake: return "wall_ns";
  }
  return {};
}

inline std::string_view ToMember(StandardLogMember member) {
  switch (member) {
    case StandardLogMember::Debug: return "debug";
    case StandardLogMember::Info: return "info";
    case StandardLogMember::Warn: return "warn";
    case StandardLogMember::Error: return "error";
    case StandardLogMember::SetLevel: return "setLevel";
    case StandardLogMember::SetFile: return "setFile";
  }
  return {};
}

inline std::string_view ToMember(StandardProcessMember member) {
  switch (member) {
    case StandardProcessMember::Run: return "run";
    case StandardProcessMember::RunText: return "runText";
  }
  return {};
}

inline std::string_view ToMember(StandardNetMember member) {
  switch (member) {
    case StandardNetMember::Connect: return "connect";
    case StandardNetMember::Listen: return "listen";
    case StandardNetMember::Read: return "read";
    case StandardNetMember::Write: return "write";
    case StandardNetMember::Close: return "close";
  }
  return {};
}

inline std::string_view ToMember(StandardHTTPMember member) {
  switch (member) {
    case StandardHTTPMember::Get: return "get";
    case StandardHTTPMember::Post: return "post";
    case StandardHTTPMember::Put: return "put";
    case StandardHTTPMember::Delete: return "delete";
    case StandardHTTPMember::Serve: return "serve";
  }
  return {};
}

inline std::string_view ToMember(StandardHTTPSMember member) {
  switch (member) {
    case StandardHTTPSMember::Get: return "get";
    case StandardHTTPSMember::Post: return "post";
    case StandardHTTPSMember::Serve: return "serve";
  }
  return {};
}

inline std::string_view ToMember(StandardTerminalMember member) {
  switch (member) {
    case StandardTerminalMember::Open: return "open";
    case StandardTerminalMember::Close: return "close";
    case StandardTerminalMember::WithRaw: return "withRaw";
    case StandardTerminalMember::WithAltScreen: return "withAltScreen";
    case StandardTerminalMember::Clear: return "clear";
    case StandardTerminalMember::Size: return "size";
    case StandardTerminalMember::MoveCursor: return "moveCursor";
    case StandardTerminalMember::WriteAt: return "writeAt";
    case StandardTerminalMember::ReadEvent: return "readEvent";
    case StandardTerminalMember::PollEvent: return "pollEvent";
  }
  return {};
}

inline std::string_view ToMember(StandardPromiseMember member) {
  switch (member) {
    case StandardPromiseMember::Run: return "run";
    case StandardPromiseMember::Await: return "await";
    case StandardPromiseMember::Poll: return "poll";
    case StandardPromiseMember::Cancel: return "cancel";
    case StandardPromiseMember::IsDone: return "isDone";
  }
  return {};
}

inline std::string_view ToMember(StandardChannelMember member) {
  switch (member) {
    case StandardChannelMember::New: return "new";
    case StandardChannelMember::Send: return "send";
    case StandardChannelMember::TrySend: return "trySend";
    case StandardChannelMember::Recv: return "recv";
    case StandardChannelMember::TryRecv: return "tryRecv";
    case StandardChannelMember::Close: return "close";
  }
  return {};
}

inline std::string_view ToMember(StandardCollectionsMember member) {
  switch (member) {
    case StandardCollectionsMember::List: return "List";
    case StandardCollectionsMember::Map: return "Map";
    case StandardCollectionsMember::Set: return "Set";
    case StandardCollectionsMember::Queue: return "Queue";
    case StandardCollectionsMember::Stack: return "Stack";
  }
  return {};
}

inline std::string_view ToMember(StandardResultMember member) {
  switch (member) {
    case StandardResultMember::Ok: return "ok";
    case StandardResultMember::Err: return "err";
    case StandardResultMember::IsOk: return "isOk";
    case StandardResultMember::Unwrap: return "unwrap";
  }
  return {};
}

inline std::string_view ToMember(StandardOptionMember member) {
  switch (member) {
    case StandardOptionMember::Some: return "some";
    case StandardOptionMember::None: return "none";
    case StandardOptionMember::IsSome: return "isSome";
    case StandardOptionMember::Unwrap: return "unwrap";
  }
  return {};
}

inline std::string_view ToMember(SystemBufferMember member) {
  switch (member) {
    case SystemBufferMember::New: return "new";
    case SystemBufferMember::Len: return "len";
    case SystemBufferMember::Get: return "get";
    case SystemBufferMember::Set: return "set";
    case SystemBufferMember::Slice: return "slice";
    case SystemBufferMember::Copy: return "copy";
    case SystemBufferMember::ReadU16LE: return "readU16LE";
    case SystemBufferMember::ReadU32LE: return "readU32LE";
    case SystemBufferMember::ReadU64LE: return "readU64LE";
    case SystemBufferMember::WriteU16LE: return "writeU16LE";
    case SystemBufferMember::WriteU32LE: return "writeU32LE";
    case SystemBufferMember::WriteU64LE: return "writeU64LE";
  }
  return {};
}

inline std::array<std::string_view, kSystemBufferMembers.size()> SystemBufferMemberNames() {
  std::array<std::string_view, kSystemBufferMembers.size()> names{};
  for (size_t i = 0; i < kSystemBufferMembers.size(); ++i) {
    names[i] = ToMember(kSystemBufferMembers[i]);
  }
  return names;
}

// Transitional native names. Phase 3 in Docs/Timeline.md removes the lowercase runtime modules.
inline std::optional<SystemModule> NativeBackingModule(StandardModule module) {
  switch (module) {
    case StandardModule::IO: return SystemModule::IO;
    case StandardModule::FS: return SystemModule::FS;
    case StandardModule::Path: return SystemModule::Path;
    case StandardModule::Buffer: return SystemModule::Buffer;
    case StandardModule::Bytes: return SystemModule::Buffer;
    case StandardModule::Json: return SystemModule::Json;
    case StandardModule::Random: return SystemModule::Random;
    case StandardModule::Time: return SystemModule::Time;
    case StandardModule::Log: return SystemModule::Log;
    case StandardModule::Process: return SystemModule::Process;
    case StandardModule::Net: return SystemModule::Net;
    case StandardModule::HTTP: return SystemModule::HTTP;
    case StandardModule::HTTPS: return SystemModule::HTTP;
    case StandardModule::Terminal: return SystemModule::Terminal;
    case StandardModule::Promise: return SystemModule::Job;
    case StandardModule::Channel: return SystemModule::Channel;
    case StandardModule::Console:
    case StandardModule::Text:
    case StandardModule::Math:
    case StandardModule::Collections:
    case StandardModule::Result:
    case StandardModule::Option:
      return std::nullopt;
  }
  return std::nullopt;
}

inline std::string_view ToNativeModule(SystemModule module) {
  switch (module) {
    case SystemModule::IO: return "System.IO";
    case SystemModule::FS: return "System.FS";
    case SystemModule::Path: return "System.Path";
    case SystemModule::Env: return "System.Env";
    case SystemModule::OS: return "System.OS";
    case SystemModule::Time: return "System.OS";
    case SystemModule::FFI: return "System.FFI";
    case SystemModule::Buffer: return "System.Buffer";
    case SystemModule::Bytes: return "System.Buffer";
    case SystemModule::Json: return "System.Json";
    case SystemModule::Log: return "System.Log";
    case SystemModule::Random: return "System.Random";
    case SystemModule::Thread: return "System.Thread";
    case SystemModule::Channel: return "System.Channel";
    default: return {};
  }
}

inline std::string_view ToNativeModule(LibraryModuleId module) {
  if (module.root == LibraryRoot::System) {
    return ToNativeModule(static_cast<SystemModule>(module.module_index));
  }
  const auto backing = NativeBackingModule(static_cast<StandardModule>(module.module_index));
  return backing ? ToNativeModule(*backing) : std::string_view{};
}

inline std::string_view ToCanonicalName(SystemModule module) {
  return ToImportPath(module);
}

inline std::string_view ToCanonicalName(StandardModule module) {
  return ToImportPath(module);
}

inline std::string_view ToCanonicalName(LibraryModuleId module) {
  return module.root == LibraryRoot::System
             ? ToCanonicalName(static_cast<SystemModule>(module.module_index))
             : ToCanonicalName(static_cast<StandardModule>(module.module_index));
}

inline std::string_view ToImportPath(LibraryModuleId module) {
  return module.root == LibraryRoot::System
             ? ToImportPath(static_cast<SystemModule>(module.module_index))
             : ToImportPath(static_cast<StandardModule>(module.module_index));
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

inline std::vector<std::string_view> MemberNames(SystemModule module) {
  switch (module) {
    case SystemModule::IO: return {ToMember(SystemIOMember::Stdin), ToMember(SystemIOMember::Stdout), ToMember(SystemIOMember::Stderr), ToMember(SystemIOMember::Write), ToMember(SystemIOMember::WriteText), ToMember(SystemIOMember::Flush), ToMember(SystemIOMember::BufferNew), ToMember(SystemIOMember::BufferLen), ToMember(SystemIOMember::BufferFill), ToMember(SystemIOMember::BufferCopy)};
    case SystemModule::FS: return {ToMember(SystemFSMember::Open), ToMember(SystemFSMember::Close), ToMember(SystemFSMember::Read), ToMember(SystemFSMember::Write), ToMember(SystemFSMember::Flush), ToMember(SystemFSMember::Seek), ToMember(SystemFSMember::Tell), ToMember(SystemFSMember::Stat), ToMember(SystemFSMember::Exists), ToMember(SystemFSMember::IsFile), ToMember(SystemFSMember::IsDir), ToMember(SystemFSMember::ListDir), ToMember(SystemFSMember::NextDirEntry), ToMember(SystemFSMember::CloseDir), ToMember(SystemFSMember::Mkdir), ToMember(SystemFSMember::MkdirAll), ToMember(SystemFSMember::Remove), ToMember(SystemFSMember::Copy), ToMember(SystemFSMember::Rename), ToMember(SystemFSMember::Cwd), ToMember(SystemFSMember::SetCwd), ToMember(SystemFSMember::ReadText), ToMember(SystemFSMember::WriteText), ToMember(SystemFSMember::ReadBytes), ToMember(SystemFSMember::WriteBytes)};
    case SystemModule::Path: return {ToMember(SystemPathMember::Separator), ToMember(SystemPathMember::Delimiter), ToMember(SystemPathMember::IsAbsolute), ToMember(SystemPathMember::Normalize), ToMember(SystemPathMember::Absolute), ToMember(SystemPathMember::Relative), ToMember(SystemPathMember::Join), ToMember(SystemPathMember::Dirname), ToMember(SystemPathMember::Basename), ToMember(SystemPathMember::Ext), ToMember(SystemPathMember::Stem)};
    case SystemModule::Env: return {ToMember(SystemEnvMember::ArgsCount), ToMember(SystemEnvMember::Arg), ToMember(SystemEnvMember::Get), ToMember(SystemEnvMember::Set), ToMember(SystemEnvMember::Unset), ToMember(SystemEnvMember::ExePath)};
    case SystemModule::OS: return {ToMember(SystemOSMember::Platform), ToMember(SystemOSMember::Arch), ToMember(SystemOSMember::IsLinux), ToMember(SystemOSMember::IsMacos), ToMember(SystemOSMember::IsWindows), ToMember(SystemOSMember::Pid), ToMember(SystemOSMember::CpuCount), ToMember(SystemOSMember::PageSize), ToMember(SystemOSMember::Exit), ToMember(SystemOSMember::SleepMs)};
    case SystemModule::Time: return {ToMember(SystemTimeMember::MonoNs), ToMember(SystemTimeMember::WallNs), ToMember(SystemTimeMember::SleepNs), ToMember(SystemTimeMember::SleepMs), ToMember(SystemTimeMember::TimerStart), ToMember(SystemTimeMember::TimerCancel), ToMember(SystemTimeMember::MonoSnake), ToMember(SystemTimeMember::WallSnake)};
    case SystemModule::FFI: return {ToMember(SystemFFIMember::Supported), ToMember(SystemFFIMember::Open), ToMember(SystemFFIMember::Symbol), ToMember(SystemFFIMember::Sym), ToMember(SystemFFIMember::Close), ToMember(SystemFFIMember::LastError), ToMember(SystemFFIMember::LastErrorSnake)};
    case SystemModule::ASM: return {ToMember(SystemASMMember::FromC), ToMember(SystemASMMember::FromDynASM), ToMember(SystemASMMember::Compile), ToMember(SystemASMMember::Symbol), ToMember(SystemASMMember::LinkStub), ToMember(SystemASMMember::LinkAot), ToMember(SystemASMMember::CloseUnit), ToMember(SystemASMMember::CloseObject)};
    case SystemModule::Buffer: return {ToMember(SystemBufferMember::New), ToMember(SystemBufferMember::Len), ToMember(SystemBufferMember::Get), ToMember(SystemBufferMember::Set), ToMember(SystemBufferMember::Slice), ToMember(SystemBufferMember::Copy), ToMember(SystemBufferMember::ReadU16LE), ToMember(SystemBufferMember::ReadU32LE), ToMember(SystemBufferMember::ReadU64LE), ToMember(SystemBufferMember::WriteU16LE), ToMember(SystemBufferMember::WriteU32LE), ToMember(SystemBufferMember::WriteU64LE)};
    case SystemModule::Bytes: return {ToMember(SystemBytesMember::New), ToMember(SystemBytesMember::Len), ToMember(SystemBytesMember::Get), ToMember(SystemBytesMember::Set), ToMember(SystemBytesMember::Slice), ToMember(SystemBytesMember::Copy), ToMember(SystemBytesMember::ReadU16LE), ToMember(SystemBytesMember::ReadU32LE), ToMember(SystemBytesMember::ReadU64LE), ToMember(SystemBytesMember::WriteU16LE), ToMember(SystemBytesMember::WriteU32LE), ToMember(SystemBytesMember::WriteU64LE)};
    case SystemModule::Json: return {ToMember(SystemJsonMember::Parse), ToMember(SystemJsonMember::Free), ToMember(SystemJsonMember::Stringify), ToMember(SystemJsonMember::Kind), ToMember(SystemJsonMember::Get), ToMember(SystemJsonMember::At), ToMember(SystemJsonMember::Len), ToMember(SystemJsonMember::AsString), ToMember(SystemJsonMember::AsI64), ToMember(SystemJsonMember::AsF64), ToMember(SystemJsonMember::AsBool)};
    case SystemModule::Log: return {ToMember(SystemLogMember::Log), ToMember(SystemLogMember::SetLevel), ToMember(SystemLogMember::SetFile), ToMember(SystemLogMember::Flush), ToMember(SystemLogMember::Info), ToMember(SystemLogMember::Warn), ToMember(SystemLogMember::Error)};
    case SystemModule::Random: return {ToMember(SystemRandomMember::Seed), ToMember(SystemRandomMember::I32), ToMember(SystemRandomMember::I64), ToMember(SystemRandomMember::F64), ToMember(SystemRandomMember::FillBytes), ToMember(SystemRandomMember::Range)};
    case SystemModule::Thread: return {ToMember(SystemThreadMember::Yield), ToMember(SystemThreadMember::SleepMs), ToMember(SystemThreadMember::Sleep), ToMember(SystemThreadMember::HardwareConcurrency), ToMember(SystemThreadMember::Spawn), ToMember(SystemThreadMember::Join), ToMember(SystemThreadMember::Detach)};
    case SystemModule::Job: return {ToMember(SystemJobMember::Spawn), ToMember(SystemJobMember::Cancel), ToMember(SystemJobMember::Poll), ToMember(SystemJobMember::Await)};
    case SystemModule::Channel: return {ToMember(SystemChannelMember::NewI32), ToMember(SystemChannelMember::SendI32), ToMember(SystemChannelMember::TrySendI32), ToMember(SystemChannelMember::RecvI32), ToMember(SystemChannelMember::TryRecvI32), ToMember(SystemChannelMember::PendingI32), ToMember(SystemChannelMember::Close)};
    case SystemModule::Process: return {ToMember(SystemProcessMember::Spawn), ToMember(SystemProcessMember::Wait), ToMember(SystemProcessMember::Kill), ToMember(SystemProcessMember::Stdin), ToMember(SystemProcessMember::Stdout), ToMember(SystemProcessMember::Stderr)};
    case SystemModule::Net: return {ToMember(SystemNetMember::TcpConnect), ToMember(SystemNetMember::TcpListen), ToMember(SystemNetMember::Accept), ToMember(SystemNetMember::Send), ToMember(SystemNetMember::Recv), ToMember(SystemNetMember::Close), ToMember(SystemNetMember::UdpOpen), ToMember(SystemNetMember::UdpSendTo), ToMember(SystemNetMember::UdpRecvFrom)};
    case SystemModule::HTTP: return {ToMember(SystemHTTPMember::ClientRequest), ToMember(SystemHTTPMember::SetHeader), ToMember(SystemHTTPMember::WriteBody), ToMember(SystemHTTPMember::Send), ToMember(SystemHTTPMember::ResponseStatus), ToMember(SystemHTTPMember::ResponseBody), ToMember(SystemHTTPMember::CloseResponse), ToMember(SystemHTTPMember::ListenHttp), ToMember(SystemHTTPMember::ListenHttps), ToMember(SystemHTTPMember::Accept), ToMember(SystemHTTPMember::WriteResponse), ToMember(SystemHTTPMember::CloseServer)};
    case SystemModule::Terminal: return {ToMember(SystemTerminalMember::Open), ToMember(SystemTerminalMember::Close), ToMember(SystemTerminalMember::EnterRaw), ToMember(SystemTerminalMember::ExitRaw), ToMember(SystemTerminalMember::EnterAltScreen), ToMember(SystemTerminalMember::ExitAltScreen), ToMember(SystemTerminalMember::Size), ToMember(SystemTerminalMember::Clear), ToMember(SystemTerminalMember::ClearLine), ToMember(SystemTerminalMember::MoveCursor), ToMember(SystemTerminalMember::ShowCursor), ToMember(SystemTerminalMember::HideCursor), ToMember(SystemTerminalMember::Write), ToMember(SystemTerminalMember::WriteAt), ToMember(SystemTerminalMember::Flush), ToMember(SystemTerminalMember::PollEvent), ToMember(SystemTerminalMember::ReadEvent)};
    case SystemModule::Capability: return {ToMember(SystemCapabilityMember::Has), ToMember(SystemCapabilityMember::Require), ToMember(SystemCapabilityMember::Deny)};
    case SystemModule::Runtime: return {ToMember(SystemRuntimeMember::Version), ToMember(SystemRuntimeMember::GcCollect), ToMember(SystemRuntimeMember::GcStats), ToMember(SystemRuntimeMember::HeapStats), ToMember(SystemRuntimeMember::JitEnabled), ToMember(SystemRuntimeMember::JitStats)};
    case SystemModule::Debug: return {ToMember(SystemDebugMember::Trap), ToMember(SystemDebugMember::Assert), ToMember(SystemDebugMember::StackTrace), ToMember(SystemDebugMember::Breakpoint)};
  }
  return {};
}

inline std::vector<std::string_view> MemberNames(StandardModule module) {
  switch (module) {
    case StandardModule::IO: return {ToMember(StandardIOMember::Print), ToMember(StandardIOMember::Println), ToMember(StandardIOMember::ReadLine)};
    case StandardModule::Console: return {ToMember(StandardConsoleMember::Write), ToMember(StandardConsoleMember::WriteLine), ToMember(StandardConsoleMember::ReadLine), ToMember(StandardConsoleMember::Clear), ToMember(StandardConsoleMember::SetColor), ToMember(StandardConsoleMember::ResetColor)};
    case StandardModule::FS: return {ToMember(StandardFSMember::ReadText), ToMember(StandardFSMember::WriteText), ToMember(StandardFSMember::AppendText), ToMember(StandardFSMember::ReadBytes), ToMember(StandardFSMember::WriteBytes), ToMember(StandardFSMember::Exists), ToMember(StandardFSMember::IsFile), ToMember(StandardFSMember::IsDir), ToMember(StandardFSMember::Copy), ToMember(StandardFSMember::Move), ToMember(StandardFSMember::Remove), ToMember(StandardFSMember::EnsureDir), ToMember(StandardFSMember::List), ToMember(StandardFSMember::Walk)};
    case StandardModule::Path: return {ToMember(StandardPathMember::Join), ToMember(StandardPathMember::Dirname), ToMember(StandardPathMember::Basename), ToMember(StandardPathMember::Ext), ToMember(StandardPathMember::Stem), ToMember(StandardPathMember::Normalize), ToMember(StandardPathMember::Absolute), ToMember(StandardPathMember::Relative)};
    case StandardModule::Buffer: return {ToMember(StandardBufferMember::New), ToMember(StandardBufferMember::WithCapacity), ToMember(StandardBufferMember::Len), ToMember(StandardBufferMember::Capacity), ToMember(StandardBufferMember::Clear), ToMember(StandardBufferMember::WriteBytes), ToMember(StandardBufferMember::WriteString), ToMember(StandardBufferMember::WriteU16LE), ToMember(StandardBufferMember::WriteU32LE), ToMember(StandardBufferMember::WriteU64LE), ToMember(StandardBufferMember::ReadU16LE), ToMember(StandardBufferMember::ReadU32LE), ToMember(StandardBufferMember::ReadU64LE), ToMember(StandardBufferMember::ToBytes), ToMember(StandardBufferMember::FromBytes)};
    case StandardModule::Bytes: return {ToMember(StandardBytesMember::New), ToMember(StandardBytesMember::FromString), ToMember(StandardBytesMember::ToString), ToMember(StandardBytesMember::Concat), ToMember(StandardBytesMember::Slice), ToMember(StandardBytesMember::ToHex), ToMember(StandardBytesMember::FromHex), ToMember(StandardBytesMember::ToBase64), ToMember(StandardBytesMember::FromBase64)};
    case StandardModule::Text: return {ToMember(StandardTextMember::Len), ToMember(StandardTextMember::IsEmpty), ToMember(StandardTextMember::Contains), ToMember(StandardTextMember::StartsWith), ToMember(StandardTextMember::EndsWith), ToMember(StandardTextMember::Trim), ToMember(StandardTextMember::Split), ToMember(StandardTextMember::Join), ToMember(StandardTextMember::Replace)};
    case StandardModule::Json: return {ToMember(StandardJsonMember::Parse), ToMember(StandardJsonMember::Stringify), ToMember(StandardJsonMember::Get), ToMember(StandardJsonMember::At), ToMember(StandardJsonMember::AsString), ToMember(StandardJsonMember::AsI64), ToMember(StandardJsonMember::AsF64), ToMember(StandardJsonMember::AsBool)};
    case StandardModule::Math: return {ToMember(StandardMathMember::PI), ToMember(StandardMathMember::Abs), ToMember(StandardMathMember::Min), ToMember(StandardMathMember::Max), ToMember(StandardMathMember::Sqrt), ToMember(StandardMathMember::Clamp), ToMember(StandardMathMember::Lerp)};
    case StandardModule::Random: return {ToMember(StandardRandomMember::Seed), ToMember(StandardRandomMember::I32), ToMember(StandardRandomMember::I64), ToMember(StandardRandomMember::Range), ToMember(StandardRandomMember::F64), ToMember(StandardRandomMember::Bool), ToMember(StandardRandomMember::Bytes), ToMember(StandardRandomMember::FillBytes)};
    case StandardModule::Time: return {ToMember(StandardTimeMember::MonoNs), ToMember(StandardTimeMember::NowNs), ToMember(StandardTimeMember::SleepMs), ToMember(StandardTimeMember::FormatWallNs), ToMember(StandardTimeMember::MonoSnake), ToMember(StandardTimeMember::WallSnake)};
    case StandardModule::Log: return {ToMember(StandardLogMember::Debug), ToMember(StandardLogMember::Info), ToMember(StandardLogMember::Warn), ToMember(StandardLogMember::Error), ToMember(StandardLogMember::SetLevel), ToMember(StandardLogMember::SetFile)};
    case StandardModule::Process: return {ToMember(StandardProcessMember::Run), ToMember(StandardProcessMember::RunText)};
    case StandardModule::Net: return {ToMember(StandardNetMember::Connect), ToMember(StandardNetMember::Listen), ToMember(StandardNetMember::Read), ToMember(StandardNetMember::Write), ToMember(StandardNetMember::Close)};
    case StandardModule::HTTP: return {ToMember(StandardHTTPMember::Get), ToMember(StandardHTTPMember::Post), ToMember(StandardHTTPMember::Put), ToMember(StandardHTTPMember::Delete), ToMember(StandardHTTPMember::Serve)};
    case StandardModule::HTTPS: return {ToMember(StandardHTTPSMember::Get), ToMember(StandardHTTPSMember::Post), ToMember(StandardHTTPSMember::Serve)};
    case StandardModule::Terminal: return {ToMember(StandardTerminalMember::Open), ToMember(StandardTerminalMember::Close), ToMember(StandardTerminalMember::WithRaw), ToMember(StandardTerminalMember::WithAltScreen), ToMember(StandardTerminalMember::Clear), ToMember(StandardTerminalMember::Size), ToMember(StandardTerminalMember::MoveCursor), ToMember(StandardTerminalMember::WriteAt), ToMember(StandardTerminalMember::ReadEvent), ToMember(StandardTerminalMember::PollEvent)};
    case StandardModule::Promise: return {ToMember(StandardPromiseMember::Run), ToMember(StandardPromiseMember::Await), ToMember(StandardPromiseMember::Poll), ToMember(StandardPromiseMember::Cancel), ToMember(StandardPromiseMember::IsDone)};
    case StandardModule::Channel: return {ToMember(StandardChannelMember::New), ToMember(StandardChannelMember::Send), ToMember(StandardChannelMember::TrySend), ToMember(StandardChannelMember::Recv), ToMember(StandardChannelMember::TryRecv), ToMember(StandardChannelMember::Close)};
    case StandardModule::Collections: return {ToMember(StandardCollectionsMember::List), ToMember(StandardCollectionsMember::Map), ToMember(StandardCollectionsMember::Set), ToMember(StandardCollectionsMember::Queue), ToMember(StandardCollectionsMember::Stack)};
    case StandardModule::Result: return {ToMember(StandardResultMember::Ok), ToMember(StandardResultMember::Err), ToMember(StandardResultMember::IsOk), ToMember(StandardResultMember::Unwrap)};
    case StandardModule::Option: return {ToMember(StandardOptionMember::Some), ToMember(StandardOptionMember::None), ToMember(StandardOptionMember::IsSome), ToMember(StandardOptionMember::Unwrap)};
  }
  return {};
}

inline LibraryModuleId ToLibraryModuleId(SystemModule module) {
  return LibraryModuleId{LibraryRoot::System, static_cast<int>(module)};
}

inline LibraryModuleId ToLibraryModuleId(StandardModule module) {
  return LibraryModuleId{LibraryRoot::Standard, static_cast<int>(module)};
}

inline std::optional<LibraryModuleId> ParseCanonicalLibraryModule(std::string_view canonical) {
  for (SystemModule module : kSystemModules) {
    if (canonical == ToCanonicalName(module)) {
      return LibraryModuleId{LibraryRoot::System, static_cast<int>(module)};
    }
  }
  for (StandardModule module : kStandardModules) {
    if (canonical == ToCanonicalName(module)) {
      return LibraryModuleId{LibraryRoot::Standard, static_cast<int>(module)};
    }
  }
  return std::nullopt;
}

inline std::vector<std::string_view> MemberNames(LibraryModuleId module) {
  return module.root == LibraryRoot::System
             ? MemberNames(static_cast<SystemModule>(module.module_index))
             : MemberNames(static_cast<StandardModule>(module.module_index));
}


template <typename T>
inline std::optional<T> ParseEnumMember(std::string_view member, std::initializer_list<T> values) {
  for (T value : values) {
    if (ToMember(value) == member) return value;
  }
  return std::nullopt;
}

inline std::optional<SystemMember> ParseMember(SystemModule module, std::string_view member) {
  switch (module) {
    case SystemModule::IO: if (auto value = ParseEnumMember<SystemIOMember>(member, {SystemIOMember::Stdin, SystemIOMember::Stdout, SystemIOMember::Stderr, SystemIOMember::Write, SystemIOMember::WriteText, SystemIOMember::Flush, SystemIOMember::BufferNew, SystemIOMember::BufferLen, SystemIOMember::BufferFill, SystemIOMember::BufferCopy})) return SystemMember(*value); break;
    case SystemModule::FS: if (auto value = ParseEnumMember<SystemFSMember>(member, {SystemFSMember::Open, SystemFSMember::Close, SystemFSMember::Read, SystemFSMember::Write, SystemFSMember::Flush, SystemFSMember::Seek, SystemFSMember::Tell, SystemFSMember::Stat, SystemFSMember::Exists, SystemFSMember::IsFile, SystemFSMember::IsDir, SystemFSMember::ListDir, SystemFSMember::NextDirEntry, SystemFSMember::CloseDir, SystemFSMember::Mkdir, SystemFSMember::MkdirAll, SystemFSMember::Remove, SystemFSMember::Copy, SystemFSMember::Rename, SystemFSMember::Cwd, SystemFSMember::SetCwd, SystemFSMember::ReadText, SystemFSMember::WriteText, SystemFSMember::ReadBytes, SystemFSMember::WriteBytes})) return SystemMember(*value); break;
    case SystemModule::Path: if (auto value = ParseEnumMember<SystemPathMember>(member, {SystemPathMember::Separator, SystemPathMember::Delimiter, SystemPathMember::IsAbsolute, SystemPathMember::Normalize, SystemPathMember::Absolute, SystemPathMember::Relative, SystemPathMember::Join, SystemPathMember::Dirname, SystemPathMember::Basename, SystemPathMember::Ext, SystemPathMember::Stem, SystemPathMember::Exists, SystemPathMember::IsFile, SystemPathMember::IsDir})) return SystemMember(*value); break;
    case SystemModule::Env: if (auto value = ParseEnumMember<SystemEnvMember>(member, {SystemEnvMember::ArgsCount, SystemEnvMember::Arg, SystemEnvMember::Get, SystemEnvMember::Set, SystemEnvMember::Unset, SystemEnvMember::ExePath})) return SystemMember(*value); break;
    case SystemModule::OS: if (auto value = ParseEnumMember<SystemOSMember>(member, {SystemOSMember::Platform, SystemOSMember::Arch, SystemOSMember::IsLinux, SystemOSMember::IsMacos, SystemOSMember::IsWindows, SystemOSMember::Pid, SystemOSMember::CpuCount, SystemOSMember::PageSize, SystemOSMember::Exit, SystemOSMember::SleepMs, SystemOSMember::ArgsCount, SystemOSMember::ArgsGet, SystemOSMember::EnvGet, SystemOSMember::CwdGet, SystemOSMember::TimeMonoNs, SystemOSMember::TimeWallNs, SystemOSMember::FormatWallNs})) return SystemMember(*value); break;
    case SystemModule::Time: if (auto value = ParseEnumMember<SystemTimeMember>(member, {SystemTimeMember::MonoNs, SystemTimeMember::WallNs, SystemTimeMember::SleepNs, SystemTimeMember::SleepMs, SystemTimeMember::TimerStart, SystemTimeMember::TimerCancel, SystemTimeMember::MonoSnake, SystemTimeMember::WallSnake})) return SystemMember(*value); break;
    case SystemModule::FFI: if (auto value = ParseEnumMember<SystemFFIMember>(member, {SystemFFIMember::Supported, SystemFFIMember::Open, SystemFFIMember::Symbol, SystemFFIMember::Sym, SystemFFIMember::Close, SystemFFIMember::LastError, SystemFFIMember::LastErrorSnake, SystemFFIMember::CallI32, SystemFFIMember::CallI64, SystemFFIMember::CallF32, SystemFFIMember::CallF64, SystemFFIMember::CallStr0})) return SystemMember(*value); break;
    case SystemModule::ASM: if (auto value = ParseEnumMember<SystemASMMember>(member, {SystemASMMember::FromC, SystemASMMember::FromDynASM, SystemASMMember::Compile, SystemASMMember::Symbol, SystemASMMember::LinkStub, SystemASMMember::LinkAot, SystemASMMember::CloseUnit, SystemASMMember::CloseObject})) return SystemMember(*value); break;
    case SystemModule::Buffer: if (auto value = ParseEnumMember<SystemBufferMember>(member, {SystemBufferMember::New, SystemBufferMember::Len, SystemBufferMember::Get, SystemBufferMember::Set, SystemBufferMember::Slice, SystemBufferMember::Copy, SystemBufferMember::ReadU16LE, SystemBufferMember::ReadU32LE, SystemBufferMember::ReadU64LE, SystemBufferMember::WriteU16LE, SystemBufferMember::WriteU32LE, SystemBufferMember::WriteU64LE})) return SystemMember(*value); break;
    case SystemModule::Bytes: if (auto value = ParseEnumMember<SystemBytesMember>(member, {SystemBytesMember::New, SystemBytesMember::Len, SystemBytesMember::Get, SystemBytesMember::Set, SystemBytesMember::Slice, SystemBytesMember::Copy, SystemBytesMember::ReadU16LE, SystemBytesMember::ReadU32LE, SystemBytesMember::ReadU64LE, SystemBytesMember::WriteU16LE, SystemBytesMember::WriteU32LE, SystemBytesMember::WriteU64LE})) return SystemMember(*value); break;
    case SystemModule::Json: if (auto value = ParseEnumMember<SystemJsonMember>(member, {SystemJsonMember::Parse, SystemJsonMember::Free, SystemJsonMember::Stringify, SystemJsonMember::Kind, SystemJsonMember::Get, SystemJsonMember::At, SystemJsonMember::Len, SystemJsonMember::AsString, SystemJsonMember::AsI64, SystemJsonMember::AsF64, SystemJsonMember::AsBool})) return SystemMember(*value); break;
    case SystemModule::Log: if (auto value = ParseEnumMember<SystemLogMember>(member, {SystemLogMember::Log, SystemLogMember::SetLevel, SystemLogMember::SetFile, SystemLogMember::Flush, SystemLogMember::Info, SystemLogMember::Warn, SystemLogMember::Error})) return SystemMember(*value); break;
    case SystemModule::Random: if (auto value = ParseEnumMember<SystemRandomMember>(member, {SystemRandomMember::Seed, SystemRandomMember::I32, SystemRandomMember::I64, SystemRandomMember::F64, SystemRandomMember::FillBytes, SystemRandomMember::Range})) return SystemMember(*value); break;
    case SystemModule::Thread: if (auto value = ParseEnumMember<SystemThreadMember>(member, {SystemThreadMember::Yield, SystemThreadMember::SleepMs, SystemThreadMember::Sleep, SystemThreadMember::HardwareConcurrency, SystemThreadMember::Spawn, SystemThreadMember::Join, SystemThreadMember::Detach})) return SystemMember(*value); break;
    case SystemModule::Job: if (auto value = ParseEnumMember<SystemJobMember>(member, {SystemJobMember::Spawn, SystemJobMember::Cancel, SystemJobMember::Poll, SystemJobMember::Await})) return SystemMember(*value); break;
    case SystemModule::Channel: if (auto value = ParseEnumMember<SystemChannelMember>(member, {SystemChannelMember::NewI32, SystemChannelMember::SendI32, SystemChannelMember::TrySendI32, SystemChannelMember::RecvI32, SystemChannelMember::TryRecvI32, SystemChannelMember::PendingI32, SystemChannelMember::NewI64, SystemChannelMember::SendI64, SystemChannelMember::TrySendI64, SystemChannelMember::RecvI64, SystemChannelMember::TryRecvI64, SystemChannelMember::PendingI64, SystemChannelMember::NewF32, SystemChannelMember::SendF32, SystemChannelMember::TrySendF32, SystemChannelMember::RecvF32, SystemChannelMember::TryRecvF32, SystemChannelMember::PendingF32, SystemChannelMember::NewF64, SystemChannelMember::SendF64, SystemChannelMember::TrySendF64, SystemChannelMember::RecvF64, SystemChannelMember::TryRecvF64, SystemChannelMember::PendingF64, SystemChannelMember::NewBool, SystemChannelMember::SendBool, SystemChannelMember::TrySendBool, SystemChannelMember::RecvBool, SystemChannelMember::TryRecvBool, SystemChannelMember::PendingBool, SystemChannelMember::NewString, SystemChannelMember::SendString, SystemChannelMember::TrySendString, SystemChannelMember::RecvString, SystemChannelMember::TryRecvString, SystemChannelMember::PendingString, SystemChannelMember::NewBytes, SystemChannelMember::SendBytes, SystemChannelMember::TrySendBytes, SystemChannelMember::RecvBytes, SystemChannelMember::TryRecvBytes, SystemChannelMember::PendingBytes, SystemChannelMember::Close})) return SystemMember(*value); break;
    case SystemModule::Process: if (auto value = ParseEnumMember<SystemProcessMember>(member, {SystemProcessMember::Spawn, SystemProcessMember::Wait, SystemProcessMember::Kill, SystemProcessMember::Stdin, SystemProcessMember::Stdout, SystemProcessMember::Stderr})) return SystemMember(*value); break;
    case SystemModule::Net: if (auto value = ParseEnumMember<SystemNetMember>(member, {SystemNetMember::TcpConnect, SystemNetMember::TcpListen, SystemNetMember::Accept, SystemNetMember::Send, SystemNetMember::Recv, SystemNetMember::Close, SystemNetMember::UdpOpen, SystemNetMember::UdpSendTo, SystemNetMember::UdpRecvFrom})) return SystemMember(*value); break;
    case SystemModule::HTTP: if (auto value = ParseEnumMember<SystemHTTPMember>(member, {SystemHTTPMember::ClientRequest, SystemHTTPMember::SetHeader, SystemHTTPMember::WriteBody, SystemHTTPMember::Send, SystemHTTPMember::ResponseStatus, SystemHTTPMember::ResponseBody, SystemHTTPMember::CloseResponse, SystemHTTPMember::ListenHttp, SystemHTTPMember::ListenHttps, SystemHTTPMember::Accept, SystemHTTPMember::WriteResponse, SystemHTTPMember::CloseServer})) return SystemMember(*value); break;
    case SystemModule::Terminal: if (auto value = ParseEnumMember<SystemTerminalMember>(member, {SystemTerminalMember::Open, SystemTerminalMember::Close, SystemTerminalMember::EnterRaw, SystemTerminalMember::ExitRaw, SystemTerminalMember::EnterAltScreen, SystemTerminalMember::ExitAltScreen, SystemTerminalMember::Size, SystemTerminalMember::Clear, SystemTerminalMember::ClearLine, SystemTerminalMember::MoveCursor, SystemTerminalMember::ShowCursor, SystemTerminalMember::HideCursor, SystemTerminalMember::Write, SystemTerminalMember::WriteAt, SystemTerminalMember::Flush, SystemTerminalMember::PollEvent, SystemTerminalMember::ReadEvent})) return SystemMember(*value); break;
    case SystemModule::Capability: if (auto value = ParseEnumMember<SystemCapabilityMember>(member, {SystemCapabilityMember::Has, SystemCapabilityMember::Require, SystemCapabilityMember::Deny})) return SystemMember(*value); break;
    case SystemModule::Runtime: if (auto value = ParseEnumMember<SystemRuntimeMember>(member, {SystemRuntimeMember::Version, SystemRuntimeMember::GcCollect, SystemRuntimeMember::GcStats, SystemRuntimeMember::HeapStats, SystemRuntimeMember::JitEnabled, SystemRuntimeMember::JitStats})) return SystemMember(*value); break;
    case SystemModule::Debug: if (auto value = ParseEnumMember<SystemDebugMember>(member, {SystemDebugMember::Trap, SystemDebugMember::Assert, SystemDebugMember::StackTrace, SystemDebugMember::Breakpoint})) return SystemMember(*value); break;
  }
  return std::nullopt;
}

inline std::optional<StandardMember> ParseMember(StandardModule module, std::string_view member) {
  switch (module) {
    case StandardModule::IO: if (auto value = ParseEnumMember<StandardIOMember>(member, {StandardIOMember::Print, StandardIOMember::Println, StandardIOMember::ReadLine})) return StandardMember(*value); break;
    case StandardModule::Console: if (auto value = ParseEnumMember<StandardConsoleMember>(member, {StandardConsoleMember::Write, StandardConsoleMember::WriteLine, StandardConsoleMember::ReadLine, StandardConsoleMember::Clear, StandardConsoleMember::SetColor, StandardConsoleMember::ResetColor})) return StandardMember(*value); break;
    case StandardModule::FS: if (auto value = ParseEnumMember<StandardFSMember>(member, {StandardFSMember::ReadText, StandardFSMember::WriteText, StandardFSMember::AppendText, StandardFSMember::ReadBytes, StandardFSMember::WriteBytes, StandardFSMember::Exists, StandardFSMember::IsFile, StandardFSMember::IsDir, StandardFSMember::Copy, StandardFSMember::Move, StandardFSMember::Remove, StandardFSMember::EnsureDir, StandardFSMember::List, StandardFSMember::Walk, StandardFSMember::Mkdir, StandardFSMember::MkdirAll, StandardFSMember::ListDir, StandardFSMember::Cwd, StandardFSMember::SetCwd})) return StandardMember(*value); break;
    case StandardModule::Path: if (auto value = ParseEnumMember<StandardPathMember>(member, {StandardPathMember::Join, StandardPathMember::Dirname, StandardPathMember::Basename, StandardPathMember::Ext, StandardPathMember::Stem, StandardPathMember::Normalize, StandardPathMember::Absolute, StandardPathMember::Relative})) return StandardMember(*value); break;
    case StandardModule::Buffer: if (auto value = ParseEnumMember<StandardBufferMember>(member, {StandardBufferMember::New, StandardBufferMember::WithCapacity, StandardBufferMember::Len, StandardBufferMember::Capacity, StandardBufferMember::Clear, StandardBufferMember::WriteBytes, StandardBufferMember::WriteString, StandardBufferMember::WriteU16LE, StandardBufferMember::WriteU32LE, StandardBufferMember::WriteU64LE, StandardBufferMember::ReadU16LE, StandardBufferMember::ReadU32LE, StandardBufferMember::ReadU64LE, StandardBufferMember::ToBytes, StandardBufferMember::FromBytes})) return StandardMember(*value); break;
    case StandardModule::Bytes: if (auto value = ParseEnumMember<StandardBytesMember>(member, {StandardBytesMember::New, StandardBytesMember::FromString, StandardBytesMember::ToString, StandardBytesMember::Concat, StandardBytesMember::Slice, StandardBytesMember::ToHex, StandardBytesMember::FromHex, StandardBytesMember::ToBase64, StandardBytesMember::FromBase64})) return StandardMember(*value); break;
    case StandardModule::Text: if (auto value = ParseEnumMember<StandardTextMember>(member, {StandardTextMember::Len, StandardTextMember::IsEmpty, StandardTextMember::Contains, StandardTextMember::StartsWith, StandardTextMember::EndsWith, StandardTextMember::Trim, StandardTextMember::Split, StandardTextMember::Join, StandardTextMember::Replace})) return StandardMember(*value); break;
    case StandardModule::Json: if (auto value = ParseEnumMember<StandardJsonMember>(member, {StandardJsonMember::Parse, StandardJsonMember::Stringify, StandardJsonMember::Get, StandardJsonMember::At, StandardJsonMember::AsString, StandardJsonMember::AsI64, StandardJsonMember::AsF64, StandardJsonMember::AsBool})) return StandardMember(*value); break;
    case StandardModule::Math: if (auto value = ParseEnumMember<StandardMathMember>(member, {StandardMathMember::PI, StandardMathMember::Abs, StandardMathMember::Min, StandardMathMember::Max, StandardMathMember::Sqrt, StandardMathMember::Clamp, StandardMathMember::Lerp})) return StandardMember(*value); break;
    case StandardModule::Random: if (auto value = ParseEnumMember<StandardRandomMember>(member, {StandardRandomMember::Seed, StandardRandomMember::I32, StandardRandomMember::I64, StandardRandomMember::Range, StandardRandomMember::F64, StandardRandomMember::Bool, StandardRandomMember::Bytes, StandardRandomMember::FillBytes})) return StandardMember(*value); break;
    case StandardModule::Time: if (auto value = ParseEnumMember<StandardTimeMember>(member, {StandardTimeMember::MonoNs, StandardTimeMember::NowNs, StandardTimeMember::SleepMs, StandardTimeMember::FormatWallNs, StandardTimeMember::MonoSnake, StandardTimeMember::WallSnake})) return StandardMember(*value); break;
    case StandardModule::Log: if (auto value = ParseEnumMember<StandardLogMember>(member, {StandardLogMember::Debug, StandardLogMember::Info, StandardLogMember::Warn, StandardLogMember::Error, StandardLogMember::SetLevel, StandardLogMember::SetFile})) return StandardMember(*value); break;
    case StandardModule::Process: if (auto value = ParseEnumMember<StandardProcessMember>(member, {StandardProcessMember::Run, StandardProcessMember::RunText})) return StandardMember(*value); break;
    case StandardModule::Net: if (auto value = ParseEnumMember<StandardNetMember>(member, {StandardNetMember::Connect, StandardNetMember::Listen, StandardNetMember::Read, StandardNetMember::Write, StandardNetMember::Close})) return StandardMember(*value); break;
    case StandardModule::HTTP: if (auto value = ParseEnumMember<StandardHTTPMember>(member, {StandardHTTPMember::Get, StandardHTTPMember::Post, StandardHTTPMember::Put, StandardHTTPMember::Delete, StandardHTTPMember::Serve})) return StandardMember(*value); break;
    case StandardModule::HTTPS: if (auto value = ParseEnumMember<StandardHTTPSMember>(member, {StandardHTTPSMember::Get, StandardHTTPSMember::Post, StandardHTTPSMember::Serve})) return StandardMember(*value); break;
    case StandardModule::Terminal: if (auto value = ParseEnumMember<StandardTerminalMember>(member, {StandardTerminalMember::Open, StandardTerminalMember::Close, StandardTerminalMember::WithRaw, StandardTerminalMember::WithAltScreen, StandardTerminalMember::Clear, StandardTerminalMember::Size, StandardTerminalMember::MoveCursor, StandardTerminalMember::WriteAt, StandardTerminalMember::ReadEvent, StandardTerminalMember::PollEvent})) return StandardMember(*value); break;
    case StandardModule::Promise: if (auto value = ParseEnumMember<StandardPromiseMember>(member, {StandardPromiseMember::Run, StandardPromiseMember::Await, StandardPromiseMember::Poll, StandardPromiseMember::Cancel, StandardPromiseMember::IsDone})) return StandardMember(*value); break;
    case StandardModule::Channel: if (auto value = ParseEnumMember<StandardChannelMember>(member, {StandardChannelMember::New, StandardChannelMember::Send, StandardChannelMember::TrySend, StandardChannelMember::Recv, StandardChannelMember::TryRecv, StandardChannelMember::Close})) return StandardMember(*value); break;
    case StandardModule::Collections: if (auto value = ParseEnumMember<StandardCollectionsMember>(member, {StandardCollectionsMember::List, StandardCollectionsMember::Map, StandardCollectionsMember::Set, StandardCollectionsMember::Queue, StandardCollectionsMember::Stack})) return StandardMember(*value); break;
    case StandardModule::Result: if (auto value = ParseEnumMember<StandardResultMember>(member, {StandardResultMember::Ok, StandardResultMember::Err, StandardResultMember::IsOk, StandardResultMember::Unwrap})) return StandardMember(*value); break;
    case StandardModule::Option: if (auto value = ParseEnumMember<StandardOptionMember>(member, {StandardOptionMember::Some, StandardOptionMember::None, StandardOptionMember::IsSome, StandardOptionMember::Unwrap})) return StandardMember(*value); break;
  }
  return std::nullopt;
}


enum class LibraryApiAvailability {
  Planned,
  Implemented,
};

enum class LibraryApiLevel {
  LowLevelSystem,
  HighLevelStandard,
};

enum class LibraryApiBacking {
  Native,
  Source,
  CompilerIntrinsic,
  Planned,
};


inline std::string NormalizeSystemFFIMemberName(std::string_view member);

struct LibraryTypeSpec {
  std::string_view name;
};

struct LibraryParamSpec {
  std::string_view name;
  LibraryTypeSpec type;
};

struct LibrarySignatureSpec {
  std::vector<LibraryParamSpec> params;
  LibraryTypeSpec return_type;
  std::vector<std::string_view> type_params;
  bool is_proc = false;
};

inline LibraryTypeSpec LibraryType(std::string_view name) { return LibraryTypeSpec{name}; }
inline LibraryParamSpec LibraryParam(std::string_view name, std::string_view type) {
  return LibraryParamSpec{name, LibraryType(type)};
}
inline LibrarySignatureSpec LibrarySignature(std::initializer_list<LibraryParamSpec> params,
                                             std::string_view return_type,
                                             std::initializer_list<std::string_view> type_params = {}) {
  LibrarySignatureSpec spec;
  spec.params.assign(params.begin(), params.end());
  spec.return_type = LibraryType(return_type);
  spec.type_params.assign(type_params.begin(), type_params.end());
  return spec;
}

inline std::optional<LibrarySignatureSpec> GetSystemLibrarySignature(SystemModule module,
                                                                     std::string_view member) {
  const std::string normalized_member = module == SystemModule::FFI
                                            ? NormalizeSystemFFIMemberName(member)
                                            : std::string(member);
  const auto parsed = ParseMember(module, normalized_member);
  if (!parsed) return std::nullopt;
  switch (module) {
    case SystemModule::IO: {
      const auto m = std::get<SystemIOMember>(*parsed);
      switch (m) {
        case SystemIOMember::BufferNew: return LibrarySignature({LibraryParam("length", "i32")}, "i32[]");
        case SystemIOMember::BufferLen: return LibrarySignature({LibraryParam("buffer", "i32[]")}, "i32");
        case SystemIOMember::BufferFill: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("value", "i32"), LibraryParam("count", "i32")}, "i32");
        case SystemIOMember::BufferCopy: return LibrarySignature({LibraryParam("dst", "i32[]"), LibraryParam("src", "i32[]"), LibraryParam("count", "i32")}, "i32");
        case SystemIOMember::Stdin:
        case SystemIOMember::Stdout:
        case SystemIOMember::Stderr:
        case SystemIOMember::Write:
        case SystemIOMember::WriteText:
        case SystemIOMember::Flush: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::FS: {
      const auto m = std::get<SystemFSMember>(*parsed);
      switch (m) {
        case SystemFSMember::Open: return LibrarySignature({LibraryParam("path", "string"), LibraryParam("flags", "i32")}, "i32");
        case SystemFSMember::Close: return LibrarySignature({LibraryParam("fd", "i32")}, "void");
        case SystemFSMember::Read:
        case SystemFSMember::Write: return LibrarySignature({LibraryParam("fd", "i32"), LibraryParam("buffer", "i32[]"), LibraryParam("count", "i32")}, "i32");
        case SystemFSMember::ReadText: return LibrarySignature({LibraryParam("path", "string")}, "string");
        case SystemFSMember::WriteText: return LibrarySignature({LibraryParam("path", "string"), LibraryParam("text", "string")}, "bool");
        case SystemFSMember::ReadBytes: return LibrarySignature({LibraryParam("path", "string")}, "i32[]");
        case SystemFSMember::WriteBytes: return LibrarySignature({LibraryParam("path", "string"), LibraryParam("bytes", "i32[]")}, "bool");
        case SystemFSMember::Exists:
        case SystemFSMember::IsFile:
        case SystemFSMember::IsDir: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case SystemFSMember::Copy: return LibrarySignature({LibraryParam("from", "string"), LibraryParam("to", "string")}, "bool");
        case SystemFSMember::ListDir: return LibrarySignature({LibraryParam("path", "string")}, "string[]");
        case SystemFSMember::Remove:
        case SystemFSMember::Mkdir:
        case SystemFSMember::MkdirAll:
        case SystemFSMember::SetCwd: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case SystemFSMember::Cwd: return LibrarySignature({}, "string");
        case SystemFSMember::Flush:
        case SystemFSMember::Seek:
        case SystemFSMember::Tell:
        case SystemFSMember::Stat:
        case SystemFSMember::NextDirEntry:
        case SystemFSMember::CloseDir:
        case SystemFSMember::Rename: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Path: {
      const auto m = std::get<SystemPathMember>(*parsed);
      switch (m) {
        case SystemPathMember::Separator:
        case SystemPathMember::Delimiter: return LibrarySignature({}, "string");
        case SystemPathMember::IsAbsolute: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case SystemPathMember::Join: return LibrarySignature({LibraryParam("lhs", "string"), LibraryParam("rhs", "string")}, "string");
        case SystemPathMember::Dirname:
        case SystemPathMember::Basename:
        case SystemPathMember::Ext:
        case SystemPathMember::Stem:
        case SystemPathMember::Normalize: return LibrarySignature({LibraryParam("path", "string")}, "string");
        case SystemPathMember::Absolute:
        case SystemPathMember::Relative:
        case SystemPathMember::Exists:
        case SystemPathMember::IsFile:
        case SystemPathMember::IsDir: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Env: {
      const auto m = std::get<SystemEnvMember>(*parsed);
      switch (m) {
        case SystemEnvMember::ArgsCount: return LibrarySignature({}, "i32");
        case SystemEnvMember::Arg: return LibrarySignature({LibraryParam("index", "i32")}, "string");
        case SystemEnvMember::Get: return LibrarySignature({LibraryParam("name", "string")}, "string");
        case SystemEnvMember::Set: return LibrarySignature({LibraryParam("name", "string"), LibraryParam("value", "string")}, "bool");
        case SystemEnvMember::Unset: return LibrarySignature({LibraryParam("name", "string")}, "bool");
        case SystemEnvMember::ExePath: return LibrarySignature({}, "string");
      }
      return std::nullopt;
    }
    case SystemModule::OS: {
      const auto m = std::get<SystemOSMember>(*parsed);
      switch (m) {
        case SystemOSMember::Platform:
        case SystemOSMember::Arch: return LibrarySignature({}, "string");
        case SystemOSMember::IsLinux:
        case SystemOSMember::IsMacos:
        case SystemOSMember::IsWindows: return LibrarySignature({}, "bool");
        case SystemOSMember::Pid:
        case SystemOSMember::CpuCount:
        case SystemOSMember::PageSize: return LibrarySignature({}, "i32");
        case SystemOSMember::Exit: return LibrarySignature({LibraryParam("code", "i32")}, "void");
        case SystemOSMember::SleepMs: return LibrarySignature({LibraryParam("milliseconds", "i32")}, "void");
        case SystemOSMember::ArgsCount:
        case SystemOSMember::ArgsGet:
        case SystemOSMember::EnvGet:
        case SystemOSMember::CwdGet:
        case SystemOSMember::TimeMonoNs:
        case SystemOSMember::TimeWallNs:
        case SystemOSMember::FormatWallNs: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Time: {
      const auto m = std::get<SystemTimeMember>(*parsed);
      switch (m) {
        case SystemTimeMember::MonoNs:
        case SystemTimeMember::WallNs:
        case SystemTimeMember::MonoSnake:
        case SystemTimeMember::WallSnake: return LibrarySignature({}, "i64");
        case SystemTimeMember::SleepNs:
        case SystemTimeMember::SleepMs:
        case SystemTimeMember::TimerStart:
        case SystemTimeMember::TimerCancel: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::FFI: {
      const auto m = std::get<SystemFFIMember>(*parsed);
      switch (m) {
        case SystemFFIMember::Open: return LibrarySignature({LibraryParam("path", "string")}, "i64");
        case SystemFFIMember::Sym:
        case SystemFFIMember::Symbol: return LibrarySignature({LibraryParam("handle", "i64"), LibraryParam("name", "string")}, "i64");
        case SystemFFIMember::Close: return LibrarySignature({LibraryParam("handle", "i64")}, "i32");
        case SystemFFIMember::LastError:
        case SystemFFIMember::LastErrorSnake: return LibrarySignature({}, "string");
        case SystemFFIMember::CallStr0: return LibrarySignature({LibraryParam("handle", "i64")}, "string");
        case SystemFFIMember::CallI32: return LibrarySignature({LibraryParam("fn_ptr", "i64"), LibraryParam("a0", "i32"), LibraryParam("a1", "i32")}, "i32");
        case SystemFFIMember::CallI64: return LibrarySignature({LibraryParam("fn_ptr", "i64"), LibraryParam("a0", "i64"), LibraryParam("a1", "i64")}, "i64");
        case SystemFFIMember::CallF32: return LibrarySignature({LibraryParam("fn_ptr", "i64"), LibraryParam("a0", "f32"), LibraryParam("a1", "f32")}, "f32");
        case SystemFFIMember::CallF64: return LibrarySignature({LibraryParam("fn_ptr", "i64"), LibraryParam("a0", "f64"), LibraryParam("a1", "f64")}, "f64");
        case SystemFFIMember::Supported: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Random: {
      const auto m = std::get<SystemRandomMember>(*parsed);
      switch (m) {
        case SystemRandomMember::Seed: return LibrarySignature({LibraryParam("seed", "i64")}, "void");
        case SystemRandomMember::I32: return LibrarySignature({}, "i32");
        case SystemRandomMember::I64: return LibrarySignature({}, "i64");
        case SystemRandomMember::F64: return LibrarySignature({}, "f64");
        case SystemRandomMember::FillBytes: return LibrarySignature({LibraryParam("bytes", "i32[]")}, "bool");
        case SystemRandomMember::Range: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Channel: {
      const auto m = std::get<SystemChannelMember>(*parsed);
      auto channel_type = [](SystemChannelMember value) -> std::string_view {
        switch (value) {
          case SystemChannelMember::NewI64: case SystemChannelMember::SendI64: case SystemChannelMember::TrySendI64: case SystemChannelMember::RecvI64: case SystemChannelMember::TryRecvI64: case SystemChannelMember::PendingI64: return "i64";
          case SystemChannelMember::NewF32: case SystemChannelMember::SendF32: case SystemChannelMember::TrySendF32: case SystemChannelMember::RecvF32: case SystemChannelMember::TryRecvF32: case SystemChannelMember::PendingF32: return "f32";
          case SystemChannelMember::NewF64: case SystemChannelMember::SendF64: case SystemChannelMember::TrySendF64: case SystemChannelMember::RecvF64: case SystemChannelMember::TryRecvF64: case SystemChannelMember::PendingF64: return "f64";
          case SystemChannelMember::NewBool: case SystemChannelMember::SendBool: case SystemChannelMember::TrySendBool: case SystemChannelMember::RecvBool: case SystemChannelMember::TryRecvBool: case SystemChannelMember::PendingBool: return "bool";
          case SystemChannelMember::NewString: case SystemChannelMember::SendString: case SystemChannelMember::TrySendString: case SystemChannelMember::RecvString: case SystemChannelMember::TryRecvString: case SystemChannelMember::PendingString: return "string";
          case SystemChannelMember::NewBytes: case SystemChannelMember::SendBytes: case SystemChannelMember::TrySendBytes: case SystemChannelMember::RecvBytes: case SystemChannelMember::TryRecvBytes: case SystemChannelMember::PendingBytes: return "i32[]";
          case SystemChannelMember::NewI32: case SystemChannelMember::SendI32: case SystemChannelMember::TrySendI32: case SystemChannelMember::RecvI32: case SystemChannelMember::TryRecvI32: case SystemChannelMember::PendingI32: case SystemChannelMember::Close: return "i32";
        }
        return "i32";
      };
      switch (m) {
        case SystemChannelMember::NewI32: case SystemChannelMember::NewI64: case SystemChannelMember::NewF32: case SystemChannelMember::NewF64: case SystemChannelMember::NewBool: case SystemChannelMember::NewString: case SystemChannelMember::NewBytes: return LibrarySignature({}, "i64");
        case SystemChannelMember::SendI32: case SystemChannelMember::TrySendI32: case SystemChannelMember::SendI64: case SystemChannelMember::TrySendI64: case SystemChannelMember::SendF32: case SystemChannelMember::TrySendF32: case SystemChannelMember::SendF64: case SystemChannelMember::TrySendF64: case SystemChannelMember::SendBool: case SystemChannelMember::TrySendBool: case SystemChannelMember::SendString: case SystemChannelMember::TrySendString: case SystemChannelMember::SendBytes: case SystemChannelMember::TrySendBytes: return LibrarySignature({LibraryParam("handle", "i64"), LibraryParam("value", channel_type(m))}, "bool");
        case SystemChannelMember::RecvI32: case SystemChannelMember::TryRecvI32: case SystemChannelMember::RecvI64: case SystemChannelMember::TryRecvI64: case SystemChannelMember::RecvF32: case SystemChannelMember::TryRecvF32: case SystemChannelMember::RecvF64: case SystemChannelMember::TryRecvF64: case SystemChannelMember::RecvBool: case SystemChannelMember::TryRecvBool: case SystemChannelMember::RecvString: case SystemChannelMember::TryRecvString: case SystemChannelMember::RecvBytes: case SystemChannelMember::TryRecvBytes: return LibrarySignature({LibraryParam("handle", "i64")}, channel_type(m));
        case SystemChannelMember::PendingI32: case SystemChannelMember::PendingI64: case SystemChannelMember::PendingF32: case SystemChannelMember::PendingF64: case SystemChannelMember::PendingBool: case SystemChannelMember::PendingString: case SystemChannelMember::PendingBytes: return LibrarySignature({LibraryParam("handle", "i64")}, "i32");
        case SystemChannelMember::Close: return LibrarySignature({LibraryParam("handle", "i64")}, "void");
      }
      return std::nullopt;
    }
    case SystemModule::Thread: {
      const auto m = std::get<SystemThreadMember>(*parsed);
      switch (m) {
        case SystemThreadMember::Sleep: return LibrarySignature({LibraryParam("milliseconds", "i32")}, "void");
        case SystemThreadMember::Yield: return LibrarySignature({}, "void");
        case SystemThreadMember::HardwareConcurrency: return LibrarySignature({}, "i32");
        case SystemThreadMember::SleepMs:
        case SystemThreadMember::Spawn:
        case SystemThreadMember::Join:
        case SystemThreadMember::Detach: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Json: {
      const auto m = std::get<SystemJsonMember>(*parsed);
      switch (m) {
        case SystemJsonMember::Parse: return LibrarySignature({LibraryParam("text", "string")}, "i64");
        case SystemJsonMember::Stringify: return LibrarySignature({LibraryParam("handle", "i64")}, "string");
        case SystemJsonMember::Free: return LibrarySignature({LibraryParam("handle", "i64")}, "bool");
        case SystemJsonMember::Kind: case SystemJsonMember::Get: case SystemJsonMember::At: case SystemJsonMember::Len: case SystemJsonMember::AsString: case SystemJsonMember::AsI64: case SystemJsonMember::AsF64: case SystemJsonMember::AsBool: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Buffer: {
      const auto m = std::get<SystemBufferMember>(*parsed);
      switch (m) {
        case SystemBufferMember::New: return LibrarySignature({LibraryParam("length", "i32")}, "i32[]");
        case SystemBufferMember::Len: return LibrarySignature({LibraryParam("buffer", "i32[]")}, "i32");
        case SystemBufferMember::ReadU16LE:
        case SystemBufferMember::ReadU32LE: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("offset", "i32")}, "i32");
        case SystemBufferMember::WriteU16LE:
        case SystemBufferMember::WriteU32LE: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("offset", "i32"), LibraryParam("value", "i32")}, "bool");
        case SystemBufferMember::Slice: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("start", "i32"), LibraryParam("length", "i32")}, "i32[]");
        case SystemBufferMember::Copy: return LibrarySignature({LibraryParam("dst", "i32[]"), LibraryParam("dstOffset", "i32"), LibraryParam("src", "i32[]"), LibraryParam("srcOffset", "i32"), LibraryParam("count", "i32")}, "i32");
        case SystemBufferMember::Get: case SystemBufferMember::Set: case SystemBufferMember::ReadU64LE: case SystemBufferMember::WriteU64LE: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Bytes: {
      const auto m = std::get<SystemBytesMember>(*parsed);
      switch (m) {
        case SystemBytesMember::New: return LibrarySignature({LibraryParam("length", "i32")}, "i32[]");
        case SystemBytesMember::Len: return LibrarySignature({LibraryParam("buffer", "i32[]")}, "i32");
        case SystemBytesMember::ReadU16LE:
        case SystemBytesMember::ReadU32LE: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("offset", "i32")}, "i32");
        case SystemBytesMember::WriteU16LE:
        case SystemBytesMember::WriteU32LE: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("offset", "i32"), LibraryParam("value", "i32")}, "bool");
        case SystemBytesMember::Slice: return LibrarySignature({LibraryParam("buffer", "i32[]"), LibraryParam("start", "i32"), LibraryParam("length", "i32")}, "i32[]");
        case SystemBytesMember::Copy: return LibrarySignature({LibraryParam("dst", "i32[]"), LibraryParam("dstOffset", "i32"), LibraryParam("src", "i32[]"), LibraryParam("srcOffset", "i32"), LibraryParam("count", "i32")}, "i32");
        case SystemBytesMember::Get: case SystemBytesMember::Set: case SystemBytesMember::ReadU64LE: case SystemBytesMember::WriteU64LE: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::Log: {
      const auto m = std::get<SystemLogMember>(*parsed);
      switch (m) {
        case SystemLogMember::Log: return LibrarySignature({LibraryParam("level", "i32"), LibraryParam("message", "string")}, "void");
        case SystemLogMember::SetLevel: return LibrarySignature({LibraryParam("level", "i32")}, "void");
        case SystemLogMember::SetFile: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case SystemLogMember::Flush: return LibrarySignature({}, "bool");
        case SystemLogMember::Info: case SystemLogMember::Warn: case SystemLogMember::Error: return std::nullopt;
      }
      return std::nullopt;
    }
    case SystemModule::ASM:
    case SystemModule::Job:
    case SystemModule::Process:
    case SystemModule::Net:
    case SystemModule::HTTP:
    case SystemModule::Terminal:
    case SystemModule::Capability:
    case SystemModule::Runtime:
    case SystemModule::Debug: return std::nullopt;
  }
  return std::nullopt;
}

inline std::optional<LibrarySignatureSpec> GetStandardLibrarySignature(StandardModule module,
                                                                       std::string_view member) {
  const auto parsed = ParseMember(module, member);
  if (!parsed) return std::nullopt;
  switch (module) {
    case StandardModule::IO: {
      const auto m = std::get<StandardIOMember>(*parsed);
      switch (m) {
        case StandardIOMember::Print:
        case StandardIOMember::Println: return LibrarySignature({LibraryParam("value", "T")}, "void", {"T"});
        case StandardIOMember::ReadLine: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Math: {
      const auto m = std::get<StandardMathMember>(*parsed);
      switch (m) {
        case StandardMathMember::Abs: return LibrarySignature({LibraryParam("value", "T")}, "T", {"T"});
        case StandardMathMember::Sqrt: return LibrarySignature({LibraryParam("value", "T")}, "T", {"T"});
        case StandardMathMember::Min:
        case StandardMathMember::Max: return LibrarySignature({LibraryParam("lhs", "T"), LibraryParam("rhs", "T")}, "T", {"T"});
        case StandardMathMember::PI:
        case StandardMathMember::Clamp:
        case StandardMathMember::Lerp: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Time: {
      const auto m = std::get<StandardTimeMember>(*parsed);
      switch (m) {
        case StandardTimeMember::MonoNs:
        case StandardTimeMember::NowNs:
        case StandardTimeMember::MonoSnake:
        case StandardTimeMember::WallSnake: return LibrarySignature({}, "i64");
        case StandardTimeMember::FormatWallNs: return LibrarySignature({LibraryParam("timestamp", "i64")}, "string");
        case StandardTimeMember::SleepMs: return LibrarySignature({LibraryParam("milliseconds", "i32")}, "void");
      }
      return std::nullopt;
    }
    case StandardModule::FS: {
      const auto m = std::get<StandardFSMember>(*parsed);
      switch (m) {
        case StandardFSMember::ReadText: return LibrarySignature({LibraryParam("path", "string")}, "string");
        case StandardFSMember::WriteText: return LibrarySignature({LibraryParam("path", "string"), LibraryParam("text", "string")}, "bool");
        case StandardFSMember::ReadBytes: return LibrarySignature({LibraryParam("path", "string")}, "i32[]");
        case StandardFSMember::WriteBytes: return LibrarySignature({LibraryParam("path", "string"), LibraryParam("bytes", "i32[]")}, "bool");
        case StandardFSMember::Exists:
        case StandardFSMember::IsFile:
        case StandardFSMember::IsDir: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case StandardFSMember::Copy: return LibrarySignature({LibraryParam("from", "string"), LibraryParam("to", "string")}, "bool");
        case StandardFSMember::ListDir: return LibrarySignature({LibraryParam("path", "string")}, "string[]");
        case StandardFSMember::Remove:
        case StandardFSMember::Mkdir:
        case StandardFSMember::MkdirAll:
        case StandardFSMember::SetCwd: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case StandardFSMember::Cwd: return LibrarySignature({}, "string");
        case StandardFSMember::AppendText: case StandardFSMember::Move: case StandardFSMember::EnsureDir: case StandardFSMember::List: case StandardFSMember::Walk: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Path: {
      const auto m = std::get<StandardPathMember>(*parsed);
      switch (m) {
        case StandardPathMember::Join: return LibrarySignature({LibraryParam("lhs", "string"), LibraryParam("rhs", "string")}, "string");
        case StandardPathMember::Dirname:
        case StandardPathMember::Basename:
        case StandardPathMember::Ext:
        case StandardPathMember::Stem:
        case StandardPathMember::Normalize: return LibrarySignature({LibraryParam("path", "string")}, "string");
        case StandardPathMember::Absolute:
        case StandardPathMember::Relative: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Random: {
      const auto m = std::get<StandardRandomMember>(*parsed);
      switch (m) {
        case StandardRandomMember::Seed: return LibrarySignature({LibraryParam("seed", "i64")}, "void");
        case StandardRandomMember::I32: return LibrarySignature({}, "i32");
        case StandardRandomMember::Range: return LibrarySignature({LibraryParam("min", "i32"), LibraryParam("max", "i32")}, "i32");
        case StandardRandomMember::I64: return LibrarySignature({}, "i64");
        case StandardRandomMember::F64: return LibrarySignature({}, "f64");
        case StandardRandomMember::Bool:
        case StandardRandomMember::Bytes:
        case StandardRandomMember::FillBytes: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Bytes: {
      const auto m = std::get<StandardBytesMember>(*parsed);
      switch (m) {
        case StandardBytesMember::New: return LibrarySignature({LibraryParam("length", "i32")}, "i32[]");
        case StandardBytesMember::Slice: return LibrarySignature({LibraryParam("bytes", "i32[]"), LibraryParam("start", "i32"), LibraryParam("length", "i32")}, "i32[]");
        case StandardBytesMember::FromString: case StandardBytesMember::ToString: case StandardBytesMember::Concat: case StandardBytesMember::ToHex: case StandardBytesMember::FromHex: case StandardBytesMember::ToBase64: case StandardBytesMember::FromBase64: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Log: {
      const auto m = std::get<StandardLogMember>(*parsed);
      switch (m) {
        case StandardLogMember::Info:
        case StandardLogMember::Warn:
        case StandardLogMember::Error: return LibrarySignature({LibraryParam("message", "string")}, "void");
        case StandardLogMember::SetLevel: return LibrarySignature({LibraryParam("level", "i32")}, "void");
        case StandardLogMember::SetFile: return LibrarySignature({LibraryParam("path", "string")}, "bool");
        case StandardLogMember::Debug: return std::nullopt;
      }
      return std::nullopt;
    }
    case StandardModule::Console:
    case StandardModule::Buffer:
    case StandardModule::Text:
    case StandardModule::Json:
    case StandardModule::Process:
    case StandardModule::Net:
    case StandardModule::HTTP:
    case StandardModule::HTTPS:
    case StandardModule::Terminal:
    case StandardModule::Promise:
    case StandardModule::Channel:
    case StandardModule::Collections:
    case StandardModule::Result:
    case StandardModule::Option: return std::nullopt;
  }
  return std::nullopt;
}

inline std::optional<LibrarySignatureSpec> GetLibrarySignature(LibraryModuleId module,
                                                               std::string_view member) {
  return module.root == LibraryRoot::System
             ? GetSystemLibrarySignature(static_cast<SystemModule>(module.module_index), member)
             : GetStandardLibrarySignature(static_cast<StandardModule>(module.module_index), member);
}

struct LibraryMemberMetadata {
  LibrarySymbol symbol;
  LibraryApiAvailability availability = LibraryApiAvailability::Planned;
  LibraryApiLevel level = LibraryApiLevel::LowLevelSystem;
  LibraryApiBacking backing = LibraryApiBacking::Planned;
  std::string_view summary;
  std::optional<LibrarySignatureSpec> signature;
};

inline bool IsImplementedSystemMember(SystemModule module, std::string_view member) {
  const auto parsed = ParseMember(module, member);
  if (!parsed) return false;
  switch (module) {
    case SystemModule::IO:
      return std::holds_alternative<SystemIOMember>(*parsed) &&
             (std::get<SystemIOMember>(*parsed) == SystemIOMember::BufferNew ||
              std::get<SystemIOMember>(*parsed) == SystemIOMember::BufferLen ||
              std::get<SystemIOMember>(*parsed) == SystemIOMember::BufferFill ||
              std::get<SystemIOMember>(*parsed) == SystemIOMember::BufferCopy);
    case SystemModule::FS:
      return std::holds_alternative<SystemFSMember>(*parsed) &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::Flush &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::Seek &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::Tell &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::Stat &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::NextDirEntry &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::CloseDir &&
             std::get<SystemFSMember>(*parsed) != SystemFSMember::Rename;
    case SystemModule::Path:
      return std::holds_alternative<SystemPathMember>(*parsed) &&
             std::get<SystemPathMember>(*parsed) != SystemPathMember::Absolute &&
             std::get<SystemPathMember>(*parsed) != SystemPathMember::Relative;
    case SystemModule::Env:
    case SystemModule::OS:
      return true;
    case SystemModule::Time:
      if (!std::holds_alternative<SystemTimeMember>(*parsed)) return false;
      switch (std::get<SystemTimeMember>(*parsed)) {
        case SystemTimeMember::MonoNs:
        case SystemTimeMember::WallNs:
        case SystemTimeMember::MonoSnake:
        case SystemTimeMember::WallSnake:
          return true;
        case SystemTimeMember::SleepNs:
        case SystemTimeMember::SleepMs:
        case SystemTimeMember::TimerStart:
        case SystemTimeMember::TimerCancel:
          return false;
      }
      return false;
    case SystemModule::FFI:
      return true;
    case SystemModule::ASM:
      return false;
    case SystemModule::Buffer:
      if (!std::holds_alternative<SystemBufferMember>(*parsed)) return false;
      switch (std::get<SystemBufferMember>(*parsed)) {
        case SystemBufferMember::New:
        case SystemBufferMember::Len:
        case SystemBufferMember::ReadU16LE:
        case SystemBufferMember::ReadU32LE:
        case SystemBufferMember::WriteU16LE:
        case SystemBufferMember::WriteU32LE:
        case SystemBufferMember::Slice:
        case SystemBufferMember::Copy:
          return true;
        case SystemBufferMember::Get:
        case SystemBufferMember::Set:
        case SystemBufferMember::ReadU64LE:
        case SystemBufferMember::WriteU64LE:
          return false;
      }
      return false;
    case SystemModule::Bytes:
      if (!std::holds_alternative<SystemBytesMember>(*parsed)) return false;
      switch (std::get<SystemBytesMember>(*parsed)) {
        case SystemBytesMember::New:
        case SystemBytesMember::Len:
        case SystemBytesMember::ReadU16LE:
        case SystemBytesMember::ReadU32LE:
        case SystemBytesMember::WriteU16LE:
        case SystemBytesMember::WriteU32LE:
        case SystemBytesMember::Slice:
        case SystemBytesMember::Copy:
          return true;
        case SystemBytesMember::Get:
        case SystemBytesMember::Set:
        case SystemBytesMember::ReadU64LE:
        case SystemBytesMember::WriteU64LE:
          return false;
      }
      return false;
    case SystemModule::Json:
      if (!std::holds_alternative<SystemJsonMember>(*parsed)) return false;
      switch (std::get<SystemJsonMember>(*parsed)) {
        case SystemJsonMember::Parse:
        case SystemJsonMember::Stringify:
        case SystemJsonMember::Free:
          return true;
        case SystemJsonMember::Kind:
        case SystemJsonMember::Get:
        case SystemJsonMember::At:
        case SystemJsonMember::Len:
        case SystemJsonMember::AsString:
        case SystemJsonMember::AsI64:
        case SystemJsonMember::AsF64:
        case SystemJsonMember::AsBool:
          return false;
      }
      return false;
    case SystemModule::Log:
      if (!std::holds_alternative<SystemLogMember>(*parsed)) return false;
      switch (std::get<SystemLogMember>(*parsed)) {
        case SystemLogMember::Log:
        case SystemLogMember::SetLevel:
        case SystemLogMember::SetFile:
        case SystemLogMember::Flush:
          return true;
        case SystemLogMember::Info:
        case SystemLogMember::Warn:
        case SystemLogMember::Error:
          return false;
      }
      return false;
    case SystemModule::Random:
      if (!std::holds_alternative<SystemRandomMember>(*parsed)) return false;
      switch (std::get<SystemRandomMember>(*parsed)) {
        case SystemRandomMember::Seed:
        case SystemRandomMember::I32:
        case SystemRandomMember::I64:
        case SystemRandomMember::F64:
        case SystemRandomMember::FillBytes:
          return true;
        case SystemRandomMember::Range:
          return false;
      }
      return false;
    case SystemModule::Thread:
      if (!std::holds_alternative<SystemThreadMember>(*parsed)) return false;
      switch (std::get<SystemThreadMember>(*parsed)) {
        case SystemThreadMember::Sleep:
        case SystemThreadMember::Yield:
        case SystemThreadMember::HardwareConcurrency:
          return true;
        case SystemThreadMember::SleepMs:
        case SystemThreadMember::Spawn:
        case SystemThreadMember::Join:
        case SystemThreadMember::Detach:
          return false;
      }
      return false;
    case SystemModule::Channel:
      return true;
    case SystemModule::Job:
    case SystemModule::Process:
    case SystemModule::Net:
    case SystemModule::HTTP:
    case SystemModule::Terminal:
    case SystemModule::Capability:
    case SystemModule::Runtime:
    case SystemModule::Debug:
      return false;
  }
  return false;
}

inline bool IsImplementedStandardMember(StandardModule module, std::string_view member) {
  const auto parsed = ParseMember(module, member);
  if (!parsed) return false;
  switch (module) {
    case StandardModule::IO:
      if (!std::holds_alternative<StandardIOMember>(*parsed)) return false;
      switch (std::get<StandardIOMember>(*parsed)) {
        case StandardIOMember::Print:
        case StandardIOMember::Println:
          return true;
        case StandardIOMember::ReadLine:
          return false;
      }
      return false;
    case StandardModule::FS:
      if (!std::holds_alternative<StandardFSMember>(*parsed)) return false;
      switch (std::get<StandardFSMember>(*parsed)) {
        case StandardFSMember::ReadText:
        case StandardFSMember::WriteText:
        case StandardFSMember::ReadBytes:
        case StandardFSMember::WriteBytes:
        case StandardFSMember::Exists:
        case StandardFSMember::IsFile:
        case StandardFSMember::IsDir:
        case StandardFSMember::Copy:
        case StandardFSMember::Remove:
        case StandardFSMember::Mkdir:
        case StandardFSMember::MkdirAll:
        case StandardFSMember::ListDir:
        case StandardFSMember::Cwd:
        case StandardFSMember::SetCwd:
          return true;
        case StandardFSMember::AppendText:
        case StandardFSMember::Move:
        case StandardFSMember::EnsureDir:
        case StandardFSMember::List:
        case StandardFSMember::Walk:
          return false;
      }
      return false;
    case StandardModule::Path:
      if (!std::holds_alternative<StandardPathMember>(*parsed)) return false;
      switch (std::get<StandardPathMember>(*parsed)) {
        case StandardPathMember::Join:
        case StandardPathMember::Dirname:
        case StandardPathMember::Basename:
        case StandardPathMember::Ext:
        case StandardPathMember::Stem:
        case StandardPathMember::Normalize:
          return true;
        case StandardPathMember::Absolute:
        case StandardPathMember::Relative:
          return false;
      }
      return false;
    case StandardModule::Bytes:
      if (!std::holds_alternative<StandardBytesMember>(*parsed)) return false;
      switch (std::get<StandardBytesMember>(*parsed)) {
        case StandardBytesMember::New:
        case StandardBytesMember::Slice:
          return true;
        case StandardBytesMember::FromString:
        case StandardBytesMember::ToString:
        case StandardBytesMember::Concat:
        case StandardBytesMember::ToHex:
        case StandardBytesMember::FromHex:
        case StandardBytesMember::ToBase64:
        case StandardBytesMember::FromBase64:
          return false;
      }
      return false;
    case StandardModule::Math:
      if (!std::holds_alternative<StandardMathMember>(*parsed)) return false;
      switch (std::get<StandardMathMember>(*parsed)) {
        case StandardMathMember::PI:
        case StandardMathMember::Abs:
        case StandardMathMember::Min:
        case StandardMathMember::Max:
        case StandardMathMember::Sqrt:
          return true;
        case StandardMathMember::Clamp:
        case StandardMathMember::Lerp:
          return false;
      }
      return false;
    case StandardModule::Random:
      if (!std::holds_alternative<StandardRandomMember>(*parsed)) return false;
      switch (std::get<StandardRandomMember>(*parsed)) {
        case StandardRandomMember::Seed:
        case StandardRandomMember::I32:
        case StandardRandomMember::I64:
        case StandardRandomMember::Range:
        case StandardRandomMember::F64:
          return true;
        case StandardRandomMember::Bool:
        case StandardRandomMember::Bytes:
        case StandardRandomMember::FillBytes:
          return false;
      }
      return false;
    case StandardModule::Time:
      if (!std::holds_alternative<StandardTimeMember>(*parsed)) return false;
      switch (std::get<StandardTimeMember>(*parsed)) {
        case StandardTimeMember::MonoNs:
        case StandardTimeMember::NowNs:
        case StandardTimeMember::SleepMs:
        case StandardTimeMember::FormatWallNs:
        case StandardTimeMember::MonoSnake:
        case StandardTimeMember::WallSnake:
          return true;
      }
      return false;
    case StandardModule::Log:
      if (!std::holds_alternative<StandardLogMember>(*parsed)) return false;
      switch (std::get<StandardLogMember>(*parsed)) {
        case StandardLogMember::Info:
        case StandardLogMember::Warn:
        case StandardLogMember::Error:
        case StandardLogMember::SetLevel:
        case StandardLogMember::SetFile:
          return true;
        case StandardLogMember::Debug:
          return false;
      }
      return false;
    case StandardModule::Console:
    case StandardModule::Buffer:
    case StandardModule::Text:
    case StandardModule::Json:
    case StandardModule::Process:
    case StandardModule::Net:
    case StandardModule::HTTP:
    case StandardModule::HTTPS:
    case StandardModule::Terminal:
    case StandardModule::Promise:
    case StandardModule::Channel:
    case StandardModule::Collections:
    case StandardModule::Result:
    case StandardModule::Option:
      return false;
  }
  return false;
}

inline bool IsImplementedLibraryMember(LibraryModuleId module, std::string_view member) {
  return module.root == LibraryRoot::System
             ? IsImplementedSystemMember(static_cast<SystemModule>(module.module_index), member)
             : IsImplementedStandardMember(static_cast<StandardModule>(module.module_index), member);
}

inline LibraryMemberMetadata GetLibraryMemberMetadata(LibraryModuleId module, std::string_view member) {
  LibraryMemberMetadata metadata;
  metadata.symbol = LibrarySymbol{module, 0xffffu, member};
  metadata.availability = IsImplementedLibraryMember(module, member)
                              ? LibraryApiAvailability::Implemented
                              : LibraryApiAvailability::Planned;
  metadata.level = module.root == LibraryRoot::System ? LibraryApiLevel::LowLevelSystem
                                                       : LibraryApiLevel::HighLevelStandard;
  metadata.backing = module.root == LibraryRoot::System ? LibraryApiBacking::Native
                                                         : LibraryApiBacking::Source;
  metadata.summary = member;
  metadata.signature = GetLibrarySignature(module, member);
  return metadata;
}

inline std::optional<LibrarySymbol> ParseLibraryMember(LibraryModuleId module,
                                                       std::string_view member) {
  const auto names = MemberNames(module);
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i] == member) {
      return LibrarySymbol{module, static_cast<uint16_t>(i), names[i]};
    }
  }
  return std::nullopt;
}

inline std::optional<LibrarySymbol> ParseLibrarySymbol(std::string_view canonical_module,
                                                       std::string_view member) {
  const auto module = ParseCanonicalLibraryModule(canonical_module);
  if (!module) return std::nullopt;
  return ParseLibraryMember(*module, member);
}

inline bool IsLibraryMember(std::string_view canonical_module, std::string_view member) {
  return ParseLibrarySymbol(canonical_module, member).has_value();
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

inline std::string NormalizeSystemFFIMemberName(std::string_view member) {
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

inline bool EqualsStaleLowercaseRuntimeModule(std::string_view stale,
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

inline std::optional<std::string_view> StaleLowercaseRuntimeModuleReplacement(std::string_view module) {
  for (SystemModule system_module : kSystemModules) {
    const std::string_view canonical = ToImportPath(system_module);
    if (EqualsStaleLowercaseRuntimeModule(module, canonical)) return canonical;
  }
  return std::nullopt;
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
